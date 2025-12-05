# BiModalText Refactor Guide

이 문서는 2차 리팩토링에서 추가‧변경된 내용을 처음 접하는 개발자도 쉽게 이해할 수 있도록 정리한 기록이다. 목표는 `rebuild_spans()`에 의존하던 이전 코드에서 탈피해 **증분(span) 갱신**을 일관되게 유지하면서, 실행 중에 스팬을 재계산하지 않아도 되는 구조로 옮겨가는 것이었다.

---

## 1. 핵심 목표

1. **증분 스팬 유지**  
   - 삽입/삭제/노드 분할/노드 제거 등 모든 편집 경로에서 각 레벨의 span을 즉시 보정한다.  
   - `rebuild_spans()`는 더 이상 런타임에 호출하지 않으며, `BIMODAL_DEBUG` 빌드에서 검증용으로만 남겨 두었다.

2. **메모리 관리 일원화**  
   - `std::pmr::unsynchronized_pool_resource` 기반의 풀 할당자로 노드를 관리한다.  
   - Node 구조체는 level 수에 따라 `next[]`와 `span[]`을 연속 메모리 상에 배치하며, 풀 자원을 통해 반복 할당 비용을 숨긴다.

3. **읽기 성능 개선**  
   - 캐싱 가능한 `Iterator`와 `scan()` 루틴을 통해 노드별 `std::visit` 횟수를 최소화했다.  
   - `optimize()`는 편집 후 `GapNode → CompactNode` 변환을 수행해 연속 메모리를 이용한 읽기 성능을 회복한다.

4. **진단 도구 강화**  
   - `debug_verify_spans()`가 레벨별 span 합, `to_string()` 일관성, `at()` 비교, 그리고 각 노드가 보관하는 span이 실제 점프 거리와 일치하는지를 모두 검사한다.  
   - fuzzer 로그는 실패 시에만 span 불일치 정보를 출력하도록 정리했다.

---

## 2. 데이터 레이어 구성

| 구성 요소 | 설명 |
|-----------|------|
| `GapNode` | 편집 모드용. `move_gap()`/`insert()`/`erase()`가 gap 영역을 직접 이동하며, `expand_buffer()`는 앞·뒤 데이터를 복사하고 gap을 넉넉히 확보한다. 분할 시 `split_right()`가 suffix를 새 GapNode로 내보낸다. |
| `CompactNode` | 읽기 모드용. Gap을 제거한 순수 배열 형태이며 SIMD 친화적인 반복/복사를 허용한다. |
| `Node` | `std::variant<GapNode, CompactNode>`를 보유한다. level 크기에 따라 `next`/`span` 배열을 맞춤 할당하며, content_size()는 variant에 위임한다. |
| PMR 풀 | `BiModalText` 멤버 `std::pmr::unsynchronized_pool_resource pool`이 정렬된 청크를 재사용한다. `create_node()`/`destroy_node()`가 풀에서 raw 메모리를 얻고 placement new/delete를 호출한다. |

---

## 3. Skip List 불변식

1. **Span 정의**  
   - `span[i]`는 “레벨 i에서 현재 노드를 기준으로 `next[i]`를 한 번 건너뛸 때 지나간 텍스트 길이”다.  
   - 레벨 0에서는 항상 현재 노드의 `content_size()`와 동일하게 유지된다.

2. **업데이트 경로 (`find_node`)**  
   - `find_node(pos, node_offset, update, rank)`는 모든 레벨에서 선행 노드를 `update[]`에 저장한다.  
   - 순회 중 `node_offset`이 대상 노드의 길이를 넘는 경우, 레벨 정보를 다시 보정하면서 다음 노드로 이동한다. 이 덕분에 삽입/삭제 시 `update` 배열을 그대로 사용할 수 있다.

3. **삽입**  
   - 대상 노드가 `CompactNode`이면 `GapNode`로 확장한 뒤 gap 위치에서 문자열을 삽입한다.  
   - 삽입된 길이만큼 `update[i]->span[i] += s.size()`로 즉시 반영한다.  
   - 노드의 논리 길이가 `NODE_MAX_SIZE`를 넘으면 `split_node()`를 호출한다.

4. **삭제**  
   - 삭제 구간마다 `update[]`를 다시 계산해 `update[i]->span[i] -= del_len`을 수행한다.  
   - 노드가 비면 `remove_node()`를 호출하고, 해당 노드가 담당하던 span은 앞선 노드로 귀속된다.

5. **분할 (`split_node`)**  
   - `GapNode::split_right()`로 뒤쪽 절반을 새 노드로 이동시킨다.  
   - 각 레벨에서 세 가지 상황을 처리한다.  
     1. `update[i]`가 존재하고 새 노드가 해당 레벨에 등장하는 경우: `update[i]->span[i] -= v_size`, `u->span[i] = v_size`, `v->span[i] = old_u_span`.  
     2. 새 레벨보다 높은 경우: `u->span[i] += v_size`로 u가 두 노드를 모두 커버한다.  
     3. `update[i]`가 u를 가리키지 않는 경우: 건너뛰기.

6. **노드 제거 (`remove_node`)**  
   - 삭제되는 노드의 길이를 `removed_len`으로 잡고, 각 레벨에서  
     `prev->span[i] = prev->span[i] - removed_len + target->span[i]`.  
   - 이렇게 하면 target의 다음 구간까지의 거리가 그대로 유지된다.

---

## 4. 읽기 경로 최적화

1. **Iterator 캐시**  
   - `Iterator`는 현재 노드 타입을 캐싱하여, `operator*`와 `operator++`가 `std::visit` 없이 값을 읽도록 한다.  
   - GapNode의 경우 앞/뒤 구간에 대한 포인터와 길이를 저장해 매 스텝마다 분기 없이 처리한다.

2. **`scan(Func)`**  
   - 노드마다 단 한 번 `std::visit`을 수행하고, 내부 루프는 단순 포인터 연산만 남도록 작성했다.  
   - 벤치마크와 fuzzer 둘 다 이 함수를 사용하여 최대한의 메모리 대역폭을 확보한다.

3. **`optimize()`**  
   - 수 많은 편집 후에는 GapNode 버퍼가 단편화되기 때문에, `optimize()`가 모든 GapNode를 `CompactNode`로 바꿔준다.  
   - 이후 읽기 집중 업무(스크롤, 저장 등)에서 캐시 효율을 극대화할 수 있다.

---

## 5. 디버깅 및 검증

1. **`debug_verify_spans()`**  
   - 레벨별 합계를 점검하고, `to_string()`과 `at()`이 같은 결과를 내는지 확인한다.  
   - 각 노드의 span이 실제 물리적 거리와 일치하는지도 확인하므로, 잘못된 증분 갱신이 바로 드러난다.

2. **Fuzzer 통합**  
   - `make debug`가 `src/fuzzer.cpp`를 AddressSanitizer/`-Og`로 빌드하고, 리팩토링된 skip list를 혹독하게 검증한다.  
   - 실패 시 `[DEBUG FAIL]` 로그만 남기고 성공 로그는 억제하여 신호 대 잡음비를 높였다.

3. **Benchmark 정합성**  
   - `Timer`는 `steady_clock`을 사용하여 음수 시간 측정을 방지하며, 타이핑 모드 테이블의 출력은 다른 시나리오와 동일한 정밀도로 통일했다.

---

## 6. 빌드 시스템 변화

| 명령 | 설명 |
|------|------|
| `make` | `src/benchmark.cpp`를 `-O3 -march=native`로 빌드하여 `main` 실행 파일 생성 |
| `make run` | `main`이 없으면 빌드하고, 벤치마크를 즉시 실행 |
| `make debug` | AddressSanitizer 켜진 `fuzzer` 바이너리를 `-Og`로 빌드 |

각 타깃은 두 실행 파일을 서로 덮어쓰지 않도록 분리되었으며, Vector/GDB 타깃 등 다른 사용자 정의 명령을 추가하기 쉬운 형태로 Makefile을 재구성했다.

---

## 7. 향후 작업 아이디어

1. **`optimize()` 고도화**  
   - 현재는 GapNode→CompactNode 전환만 수행한다. Span을 기준으로 작은 노드를 병합하거나, 읽기 전용 모드에 맞춰 레벨을 재조정하는 로직을 추가할 수 있다.

2. **PMR 튜닝**  
   - `unsynchronized_pool_resource`의 블록 크기 정책을 수동으로 지정하면, 특정 workload(짧은 편집 세션 등)에 맞춘 대역폭 개선이 가능하다.

3. **Span 재빌드 백그라운드 작업**  
   - 현재 구조는 증분 갱신만으로 충분하지만, 디버그 빌드에서만 사용할 “부분 재빌드” 도구를 만들어서 Span 구조를 선택적으로 재시동할 수 있다.

이 문서가 향후 유지보수와 추가 최적화의 출발점이 되길 바란다. 질문이나 반례가 생기면 `debug_verify_spans()` 로그와 함께 사례를 남겨주면 빠르게 원인을 추적할 수 있다.
