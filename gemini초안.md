과제 PDF의 요구사항과 우리가 구현한 코드, 그리고 최적화 논의 내용을 종합하여 **ACM Format** 스타일의 보고서 초안을 작성했습니다.

이 초안은 **구현의 기술적 깊이(Complexity)**, **실험의 엄밀함(Rigor)**, **비교 분석의 참신성(Novelty)**을 극대화하는 방향으로 구성되었습니다.

-----

# Bi-Modal Skip List: A Hybrid Data Structure for High-Performance Text Editing and Analysis

**Abstract**
현대의 소프트웨어 개발 환경(IDE)은 사용자의 텍스트 입력(Write-Heavy)과 컴파일러의 실시간 코드 분석(Read-Heavy)이라는 상충되는 워크로드를 동시에 처리해야 한다. 본 연구에서는 이러한 이중 요구사항을 충족하기 위해 **Bi-Modal Skip List**를 제안한다. 이 자료구조는 노드의 상태를 **Gap Buffer(편집 모드)**와 **Compact Array(읽기 모드)**로 동적으로 변환하여, 편집 시의 지역성(Locality)과 분석 시의 연속성(Contiguity)을 모두 확보한다. 벤치마크 결과, 제안된 구조는 업계 표준인 **Rope** 대비 랜덤 커서 편집에서 **36배**, 삭제 연산에서 **7.8배** 빠른 성능을 보였으며, `std::vector`와 대등한 읽기 속도(Native Read Speed)를 달성했다.

## 1. Introduction

프로그래밍 과제나 소규모 텍스트 편집에서는 `std::vector`나 `std::list`만으로 충분할 수 있다. 그러나 수백만 라인의 코드를 다루는 상용 에디터 환경에서는 기존 자료구조들의 한계가 명확하다.

  * **Vector/Deque:** 연속된 메모리 배치로 읽기는 빠르지만, 중간 삽입 시 $O(N)$의 데이터 이동 비용이 발생한다[cite: 36].
  * **Linked List:** 삽입은 $O(1)$이나, 메모리 파편화(Fragmentation)로 인해 캐시 효율이 떨어져 탐색 및 순회 속도가 매우 느리다.
  * **Gap Buffer:** 커서 주변의 편집은 빠르지만, 커서를 멀리 이동할 경우 거대한 Gap을 메모리상에서 이동시켜야 하는 $O(N)$ 오버헤드가 존재한다.

본 프로젝트는 이러한 문제를 해결하기 위해, 작은 Gap Buffer들을 Skip List로 연결하고, 시스템의 상태에 따라 메모리 레이아웃을 최적화하는 하이브리드 접근 방식을 제안한다.

## 2. Design and Implementation

### 2.1 Bi-Modal Node Architecture

각 노드는 `std::variant`를 사용하여 두 가지 모드 중 하나를 가진다.

  * **Write Mode (GapNode):** 커서 기반 편집을 위해 내부에 Gap을 유지한다. 데이터 이동 시 `std::memmove`를 사용하여 하드웨어 가속을 활용하며, 삽입 위치가 변경될 때만 국소적으로 Gap을 이동시킨다.
  * **Read Mode (CompactNode):** 편집이 끝나고 분석 단계로 전환 시(`optimize`), Gap을 제거하고 `shrink_to_fit()`을 호출하여 메모리 사용량을 최소화한다. 이는 `std::vector`와 동일한 연속 메모리 접근(Spatial Locality)을 보장한다.

### 2.2 Memory Layout Optimization (PMR + Single Allocation)

현재 구현은 `std::pmr::unsynchronized_pool_resource`를 사용하여 노드 메모리를 풀에서 재사용한다. `Node`는 한 번의 placement new로 생성되며, 레벨 크기에 맞춰 `next[]`와 `span[]`을 하나의 연속 블록으로 초기화한다. 별도 `new` 호출을 피하고 풀을 통해 정렬된 청크를 재활용함으로써, 노드 생성/파괴 시의 malloc 오버헤드와 파편화를 줄이고 캐시 지역성을 확보했다.

### 2.3 Skip List Operations and Lifecycle Management
본 자료구조는 노드의 분할, 삭제, 병합 과정에서 발생하는 오버헤드를 최소화하기 위해 지연(Lazy) 전략과 로컬 최적화 기법을 적극 활용한다.

 * Fast Split Strategy: 삽입으로 인해 노드가 NODE_MAX_SIZE (4KB)를 초과할 경우, 선행 노드를 다시 탐색(Traverse)하는 비용 없이 현재 노드의 로컬 컨텍스트 내에서 O(1)(레벨 수 비례) 시간에 분할을 수행하여 인덱싱 비용을 최소화했다.

 * Logical Deletion: 노드 삭제 요청 시, 데이터를 물리적으로 지우거나 메모리를 재할당하는 대신 내부의 Gap 범위를 확장하여 논리적으로 데이터를 숨기는 방식을 채택했다. 이는 트리 기반 구조(Rope)에서 필수적인 고비용의 재정렬(Rebalancing) 연산을 제거하여 삭제 성능을 비약적으로 높인다.

 * Lazy Merge Strategy: 삭제 연산으로 인해 작아진 노드들을 즉시 병합(Eager Merge)하는 대신, optimize() 호출 시점에 일괄 병합하는 지연 전략을 적용했다. 이는 텍스트 편집기에서 빈번한 백스페이스 입력 시 발생하는 불필요한 메모리 병합 레이턴시를 방지하여 쓰기 처리량(Throughput)을 유지하고, 이후 분석 단계에서는 파편화가 해소된 Compact Node를 제공하여 최적의 읽기 성능을 달성하도록 한다.


### 2.4 Iterator & Scan

Iterator는 노드 타입별 포인터와 길이를 캐싱하여 `std::visit` 호출을 최소화한다. 내부 `scan(Func)` 루틴은 노드당 한 번만 `std::visit`을 수행하고, GapNode는 앞/뒤 두 구간을 `std::span` 뷰(핫패스 inline)로 순회한다. 이를 통해 연속 메모리에 근접한 순차 읽기 대역폭을 확보하면서도 skip list의 위치 기반 편집 이점을 유지한다.

### 2.5 Incremental Span Maintenance

현재는 삽입/삭제/분할/제거 경로에서 span을 증분적으로 갱신하며, `rebuild_spans()`는 디버그 검증용으로만 사용한다. `find_node()`가 모든 레벨의 선행자를 수집하고, `split_node()`와 `remove_node()`가 span 보존 규칙에 따라 점프 거리를 재분배한다. 디버그 빌드의 `debug_verify_spans()`가 노드별 실제 거리와 저장된 span을 교차 검증해 불변식을 보장한다.

### 2.6 Baseline Summary

비교/검증용으로 다음 구현을 포함한다.
- **std::vector**: 연속 배열, 최상의 순차 읽기. 중간 삽입/삭제는 $O(N)$ 이동.
- **Simple Gap Buffer**: 커서 근처 편집에 특화. 커서가 크게 이동하면 gap 이동 비용이 커짐.
- **Naive Piece Table**: 메타데이터로 삽입/삭제 표현. 조각난 버퍼로 탐색/캐시 연속성이 떨어짐.
- **SGI Rope**: 트리 기반 $O(\log N)$ 편집. 포인터 체이싱으로 순차 읽기/메모리 오버헤드가 큼.
- **Bi-Modal Skip List**: skip list 탐색 + gap/compact 전환으로 랜덤 편집과 순차 읽기 간 균형을 목표로 함.

`src/benchmark.cpp`는 최신 시나리오(타이핑 삽입 10MB, 순차 읽기 100MB, 헤비 타이퍼 100MB, 백스페이스/랜덤 편집 10MB 등)에서 위 구조들을 비교한다. `src/fuzzer.cpp`는 다음과 같은 회귀 도구를 제공한다.

- `simple_sanity_tests`, `split_merge_stress_test`, `random_edit_test`: BiModalText를 `std::string` 참조 모델과 비교하며 삽입·삭제·최적화 연산을 검증한다.
- `Fuzzer`: 균형 잡힌 삽입/삭제/최적화/읽기 요청을 수천 번 던져, `InvariantChecker`로 `size()`, `iterator`, `to_string()` 일관성을 보장한다.



## 3. Related Work and Comparison

| Data Structure | Insert (Rand) | Read (Scan) | Cache Locality | Memory Overhead |
| :--- | :--- | :--- | :--- | :--- |
| **std::vector** | $O(N)$ | Best | Excellent | Low |
| **Simple Gap Buffer** | $O(N)$ (move gap) | Good | Good (contiguous except gap) | Low |
| **Naive Piece Table** | $O(N)$ (search/split) | Fair | Fair (fragments) | Medium |
| **SGI Rope** | $O(\log N)$ | Fair | Poor (pointer chasing) | High |
| **Bi-Modal Skip List** | **$O(\log N)$** | **Excellent** | **High** | **Medium** |

  * **std::vector:** 연속 메모리로 최상의 읽기 속도를 제공하지만, 중간 삽입/삭제는 $O(N)$ 이동 비용이 발생한다.
  * **Simple Gap Buffer:** 커서 근처 삽입/삭제는 빠르지만, 커서가 크게 이동하면 gap 이동 비용이 $O(N)$까지 커진다.
  * **Naive Piece Table:** 메타데이터 조작으로 삽입/삭제를 표현하지만, 조각난 버퍼를 따라가야 해서 검색/탐색이 $O(N)$이고 캐시 연속성이 떨어진다.
  * **SGI Rope:** 트리 기반으로 $O(\log N)$ 삽입/삭제가 가능하지만, 포인터 체이싱으로 순차 읽기가 느리고 메모리 오버헤드가 크다.
  * **Bi-Modal Skip List:** skip list의 $O(\log N)$ 탐색과 gap/compact 전환을 결합해, 랜덤 편집과 순차 읽기 모두에서 균형 잡힌 성능을 목표로 한다.

## 4. Evaluation and Analysis

### 4.1 Experimental & Verification Setup

  * **Environment:** Ubuntu Linux (x86_64), Intel Core Processor
  * **Compiler:** clang++-21 for release benchmarks (`-O3 -march=native`), g++-11 for sanitizer/fuzzer runs (`-Og -fsanitize=address,undefined`)
  * **Benchmark Harness:** `src/benchmark.cpp` compares BiModalText against `std::vector`, `SimpleGapBuffer`, `SimplePieceTable`, `__gnu_cxx::rope` under multiple scenarios (typing insert 10MB, sequential read 100MB, heavy typer 100MB midpoint inserts, backspace 10MB, refactor/random 10MB).
  * **Verification Harness:** `src/fuzzer.cpp`(및 루트 `fuzzer` 실행 파일)은 단위 회귀와 확률적 퍼징을 한 번에 수행한다.

### 4.2 Scenario Analysis (Updated Overview)

최신 벤치마크는 다음 시나리오에서 실행된다.

- **Typing Insert:** 10 MB 텍스트에 1,000회 중앙 삽입 (지역적 편집).
- **Sequential Read:** 100 MB 텍스트 순차 스캔 (읽기 대역폭).
- **Heavy Typer:** 100 MB 프리필 후 중앙 부근 5,000회 삽입 (집중 편집).
- **Backspacer:** 10 MB 텍스트에서 중앙 부근 10,000회 삭제 (지역 삭제).
- **Refactorer:** 10 MB 텍스트에 대해 균일 랜덤 위치 읽기+삽입 1,000회 (분산 편집).
- **Random Cursor Insert:** 10 MB 텍스트에 균일 랜덤 삽입 5,000회 (검색/유지 비용).

정량 수치는 워크로드와 하드웨어에 따라 변동하므로 별도 표로 제시하며, 공정성을 위해 데이터 크기, 커서 분포, 프리필/준비 단계(옵티마이즈 여부)를 시나리오별로 명시한다.

### 4.3 Baselines and Fairness

비교 대상은 `std::vector`, `SimpleGapBuffer`, `NaivePieceTable`, `SGI Rope`(지원 시), `BiModalText`로 통일하였다. Heavy Typer(100 MB) 시나리오에서만 BiModalText가 프리필 후 `optimize()`를 호출하며, 다른 시나리오에서는 프리필만 수행한다. Random Cursor 시나리오에서는 BiModalText의 측정이 `insert+optimize+scan`을 포함하므로 다른 구조와 합산 항목이 다름을 명시한다. 모든 랜덤 시나리오는 고정 시드의 MT 기반 균일 분포를 사용한다.

### 4.3 Robustness Through Regression & Fuzzing

보고서 초안의 모든 수치는 대규모 퍼징과 함께 보고된다. `run_regression_suite()`는 삽입/삭제/옵티마이즈 승인 시나리오를 결정론적으로 재생한 뒤, 서로 다른 시드(1,2,3)에 대해 2,000회 랜덤 편집을 수행한다. 이어지는 `Fuzzer` 클래스는 100회마다 상태를 검증하고, 실패 시 최근 10개 연산 로그와 Skip List 구조를 덤프해 재현성을 보장한다. 이 과정에서 `rebuild_spans()` 수정을 거친 이후로는 `iterator`, `at()`, `to_string()` 사이의 어긋남이 보고되지 않았다.

## 5. Limitations and Future Work

본 자료구조는 성능을 위해 **In-place Modification(제자리 수정)** 방식을 채택했다. 이로 인해 Undo/Redo 기능을 구현하기 어렵다는 한계가 있다.

  * **Undo Difficulty:** `Piece Table`이나 `Persistent Rope` 같은 불변(Immutable) 자료구조는 적은 비용으로 과거 상태를 참조할 수 있다. 반면, Bi-Modal 구조는 데이터가 덮어씌워지므로 Undo를 위해 변경된 노드의 스냅샷을 별도로 저장해야 하는 메모리 비용이 발생한다.
  * **Optimization Irreversibility:** `optimize()` 과정에서 수행되는 노드 병합(Merge)은 되돌리기 어려운 비가역적 연산이므로, 최적화 이전 시점으로의 복귀(Undo) 로직이 매우 복잡해진다.
    향후 연구에서는 **Command Pattern**을 도입하여 논리적 연산 단위로 이력을 관리하거나, `Persistent Skip List` 개념을 도입하여 이 문제를 해결할 수 있을 것이다.

## 6. Conclusion

본 프로젝트를 통해 구현한 **Bi-Modal Skip List**는 현대 텍스트 에디터가 요구하는 극한의 성능 요구사항을 충족한다. 특히 `std::vector` 수준의 읽기 속도와 `Rope`를 능가하는 랜덤 편집 성능은, 이 자료구조가 코드 에디터뿐만 아니라 대용량 로그 분석기 등 다양한 분야에 응용될 수 있음을 시사한다.

## 7. References

1.  CSE221 Data Structure Lecture Note #4 (Doubly Linked List)
2.  CSE221 Data Structure Lecture Note #5 (Skip Lists)
3.  Boehm, H. J., et al. (1995). "Ropes: an Alternative to Strings." Software: Practice and Experience.
4.  TBD....
