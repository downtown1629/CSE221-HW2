# Optimization Retrospective

## 1. Attempt Log

| 날짜 | 시도 | 기대효과 | 결과 |
| --- | --- | --- | --- |
| Day 1 | Span 재구성 제거 + `refresh_spans()` 도입 | 매 mutate 후 `rebuild_spans()` 비용 제거 | **실패** – 상위 레벨 span 누적 오차 발생, fuzzer 실패 |
| Day 2 | Partial merge spans 유지 + dirty flag | `optimize()` 시 재계산 최소화 | **실패** – merge 이후 iter 경로 오염, deterministic tests 실패 |
| Day 3 | `SimplePieceTable` insert/scan 분리 | benchmark 시간을 줄여 최적화 효과 부각 | **성공(부분)** – insert skip 덕분에 벤치마크 가속, 기능 유지 |
| Day 4 | `BiModalText` lazy refresh 방식 | spans dirty 플래그로 전체 rebuild 횟수 감소 | **실패** – gap/compact 변환 지점에서 log-search 악화 |

## 2. Failure Analysis

1. **Span drift**: `refresh_spans()`가 경로 상 일부 노드만 갱신 → `find_node()`가 상위 레벨 조건 만족 시 잘못된 jump 수행. `iterator`/`at()` mismatch의 직접 원인.

2. **Merge invariants 불일치**: `optimize()` merge에서 레벨 0만 수정, 상위 레벨 span은 rebuild에 의존. Dirty 플래그가 늦게 발화되어 겹친 작업이 누적될수록 iterator 비용 증가.

3. **Benchmark regression**: Pre-optimization compaction이 모든 노드를 CompactNode로 만들어 log-search 이전에 분기 비용 추가. 캐시 히트율 감소로 sequential read가 눈에 띄게 악화.

4. **Allocator churn**: Split 시 새 `Node` + 버퍼를 항상 생성해 RAND 편집을 반복할수록 힙 단편화 증가. 랜덤 삽입/삭제 성능 악화의 간접 요인.

## 3. 새로운 최적화 계획

### 3.1 Span Maintenance Rework
- **목표**: 완전 rebuild 없이 `log-search` 정확도 유지.
- **전략**:
  1. `Node`별 prefix length를 저장하는 `accum_size` 필드 추가.
  2. insert/erase/split/merge 후 관련 서브트리만 순회하여 `accum_size` 갱신.
  3. `find_node()`는 `accum_size` 기반으로 조건 확인 → 레벨별 `span`은 `accum_size` 차이로 계산, 별도 rebuild 필요 없음.

### 3.2 Lazy Mode Conversion
- **문제**: 모든 노드를 즉시 Compact로 변환하면 편집 재진입 시 Gap 생성 비용 발생.
- **개선**:
  - `Node` 내부에 `mode` enum 추가 (`Gap`, `Compact`, `Hybrid`).
  - `optimize()`는 `Hybrid` 플래그만 설정하고 실제 버퍼 변환은 이후 읽기/쓰기 요청 시 수행.
  - Gap로 복귀해야 할 때는 기존 버퍼를 재활용하며 `DEFAULT_GAP_SIZE`만큼의 여유 공간을 덧붙여 힙 할당을 줄임.

### 3.3 Iterator Fast Path 개선
- Gap/Compact 모두를 위한 캐시 구조 (`cached_ptr`, `cached_len`, `gap_front_len`, `gap_back_ptr`) 도입.
- `operator*`에서 분기 줄이고 단일 포인터 연산으로 데이터 접근 → sequential read 회복.

### 3.4 Node Recycling
- `split_node()`와 `remove_node()`에서 Node pool을 사용.
- Max 64개 정도의 free list 유지 → randomness가 높을 때도 힙 단편화 완화.

### 3.5 Benchmark Alignment
- Typing Scenario에서 초기 `optimize()` 호출 제거, 대신 사용자가 명시적으로 최적화 요청 시 실행하도록 조정.
- Random Cursor Benchmark는 `optimize()`와 `scan()`을 분리하여 각각의 비용을 독립 측정.

## 4. Validation Plan

1. **Unit Tests**: `fuzzer.cpp`의 deterministic suite + boundary tests.
2. **Fuzzers**: 3 seeds × (5k, 2.5k, 10k) iterations.
3. **Performance**: `src/benchmark.cpp` 타이핑, 랜덤 삽입, 백스페이스 시나리오.
4. **Instrumentation**: `BIMODAL_DEBUG`에서 `accum_size` 일관성 검증, allocator pool 상태 출력으로 leak 확인.

## 5. Expected Impact

| 영역 | 예상 효과 |
| --- | --- |
| 순차 읽기 | Iterator fast path + lazy conversion → 기존 대비 회복 또는 개선 |
| 랜덤 편집 | Span rework + allocator pool → log-search 비용 절감 |
| 타이핑 | 초기 optimize 제거로 힙 작업 감소, latency 감소 |
| 안정성 | Dirty rebuild 제거 → 성능 deterministic, fuzzer 안정 |

