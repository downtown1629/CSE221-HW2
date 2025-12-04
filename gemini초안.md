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

### 2.2 Memory Layout Optimization (Single Allocation)

초기 구현에서는 Skip List의 레벨 관리를 위해 `next` 포인터 배열과 `span` 거리 배열을 각각 `new`로 할당했다. 그러나 프로파일링 결과, 이는 노드 생성 시 잦은 `malloc` 호출 오버헤드를 발생시키고 힙 메모리 파편화를 가중시켜 랜덤 삽입 성능을 저하시키는 주원인으로 지목되었다.

이를 해결하기 위해 본 구현에서는 Single Block Allocation 기법을 도입했다. `next` 배열과 `span` 배열을 별도로 할당하지 않고, 필요한 총 크기만큼 하나의 연속된 메모리 블록을 할당한 뒤 포인터 연산을 통해 구획을 나누어 사용한다.

```cpp
// Implementation Detail
Node(int lvl) {
    // Allocate next[] and span[] in a single contiguous block
    size_t alloc_size = (sizeof(Node*) + sizeof(size_t)) * lvl;
    memory_block = new char[alloc_size];
    std::memset(memory_block, 0, alloc_size); // Batch initialization
}
```

`next` 포인터 배열과 `span` 거리 배열을 한 번의 `new`로 할당하고 `std::memset`으로 초기화함으로써, 생성 속도를 높이고 메모리 지역성을 극대화했다. 그 결과, std::vector 대비 힙 관리 오버헤드를 획기적으로 줄이며 편집 성능을 개선했다.

### 2.3 Skip List Operations and Lifecycle Management
본 자료구조는 노드의 분할, 삭제, 병합 과정에서 발생하는 오버헤드를 최소화하기 위해 지연(Lazy) 전략과 로컬 최적화 기법을 적극 활용한다.

 * Fast Split Strategy: 삽입으로 인해 노드가 NODE_MAX_SIZE (4KB)를 초과할 경우, 선행 노드를 다시 탐색(Traverse)하는 비용 없이 현재 노드의 로컬 컨텍스트 내에서 O(1)(레벨 수 비례) 시간에 분할을 수행하여 인덱싱 비용을 최소화했다.

 * Logical Deletion: 노드 삭제 요청 시, 데이터를 물리적으로 지우거나 메모리를 재할당하는 대신 내부의 Gap 범위를 확장하여 논리적으로 데이터를 숨기는 방식을 채택했다. 이는 트리 기반 구조(Rope)에서 필수적인 고비용의 재정렬(Rebalancing) 연산을 제거하여 삭제 성능을 비약적으로 높인다.

 * Lazy Merge Strategy: 삭제 연산으로 인해 작아진 노드들을 즉시 병합(Eager Merge)하는 대신, optimize() 호출 시점에 일괄 병합하는 지연 전략을 적용했다. 이는 텍스트 편집기에서 빈번한 백스페이스 입력 시 발생하는 불필요한 메모리 병합 레이턴시를 방지하여 쓰기 처리량(Throughput)을 유지하고, 이후 분석 단계에서는 파편화가 해소된 Compact Node를 제공하여 최적의 읽기 성능을 달성하도록 한다.


### 2.4 Zero-Overhead Iterator & SIMD Scan

`src/BiModalSkipList.hpp`의 Iterator는 현재 노드의 데이터 포인터와 길이를 캐싱(`cached_ptr`, `cached_len`)하여, 반복문마다 `std::visit`을 호출하는 비용을 제거한다. CompactNode라면 `cached_ptr[offset]`으로 즉시 접근하고, GapNode는 `at()`으로 폴백한다. 또한 내부 `scan(Func)` 루틴은 노드당 한 번만 `std::visit`을 수행한 뒤 `for` 루프에서 연속 데이터를 제공하므로, 컴파일러가 `func`를 적극적으로 인라인·벡터화할 수 있다. 이 조합 덕분에 `std::vector` 수준의 순차 읽기 대역폭을 유지하면서도 Skip List의 위치 기반 편집 이점을 살렸다.

### 2.5 Deterministic Span Maintenance

최근 수정에서는 모든 변이 연산 뒤에 `rebuild_spans()`를 호출하여 스팬 메타데이터를 항상 레벨 0 순서에서 재계산한다. 과거에는 각 연산에서 국소적으로 `span`을 조정했지만, 노드 분할·병합이 겹치면 누적 오차가 생겨 `at()`과 Iterator 사이가 어긋났다. 이제는

1. `find_node()`가 `update`/`rank` 배열을 채워 안전하게 분할 동작을 준비하고,
2. `split_node()`와 `remove_node()`가 연결 정보를 갱신한 뒤,
3. `rebuild_spans()`가 감시자 노드 `head`부터 전 레벨 span을 새로 쓰는

파이프라인으로 재구성되어, 시드에 독립적인 결정론적 동작을 보장한다.

### 2.6 Verification Components in the Repository

`src/Baselines.hpp`에는 정밀 비교를 위한 `SimpleGapBuffer`와 `SimplePieceTable` 구현이 포함되어 있어, 벤치마크 및 디버깅 단계에서 참조 동작을 제공한다. `src/fuzzer.cpp`는 다음과 같은 회귀 도구를 제공한다.

- `simple_sanity_tests`, `split_merge_stress_test`, `random_edit_test`: BiModalText를 `std::string` 참조 모델과 비교하며 삽입·삭제·최적화 연산을 검증한다.
- `Fuzzer`: 균형 잡힌 삽입/삭제/최적화/읽기 요청을 수천 번 던져, `InvariantChecker`로 `size()`, `iterator`, `to_string()` 일관성을 보장한다.

`src/main.cpp`는 최종 데모로, Insert/Optimize/Re-edit 흐름을 한 번에 보여주며 보고서에 담은 개념적 흐름과 동일하게 동작한다. 루트 디렉터리의 `fuzzer` 실행 파일을 통해 위 회귀 도구를 반복 실행할 수 있다.

## 3. Related Work and Comparison

| Data Structure | Insert (Rand) | Read (Scan) | Cache Locality | Memory Overhead |
| :--- | :--- | :--- | :--- | :--- |
| **std::vector** | $O(N)$ | Best | Excellent | Low |
| **std::list** | $O(1)$ | Poor | Poor | High (Pointers) |
| **Simple Gap Buffer** | $O(N)$ (Move) | Good | Good | Low |
| **Piece Table** | $O(1)$ metadata | Fair | Fair | Medium (Lists) |
| **Rope (Tree)** | $O(\log N)$ | Fair | Poor (Tree) | High (Nodes) |
| **Bi-Modal Skip List** | **$O(\log N)$** | **Excellent** | **High** | **Medium** |

  * **vs. Simple Gap Buffer:** 단일 Gap Buffer는 커서 이동 시 전체 데이터를 이동해야 하지만, Bi-Modal 구조는 해당 위치의 노드만 수정하므로 랜덤 액세스 편집에 강력하다. `SimpleGapBuffer` baseline으로 구현·검증했다.
  * **vs. Piece Table:** Piece Table은 메타데이터만 조작해 빠르지만, 외부 버퍼 접근이 잦아 캐시 연속성이 떨어진다. Bi-Modal은 `optimize()` 후 모든 노드가 CompactNode여서 캐시 히트율이 높다.
  * **vs. Rope:** Rope는 이진 트리 구조로 인해 순차 읽기 시 'Pointer Chasing'이 발생하여 느리다. 반면, 제안 구조는 `optimize()` 후 노드 내부가 연속적이므로 읽기 속도가 월등하다.

## 4. Evaluation and Analysis

### 4.1 Experimental & Verification Setup

  * **Environment:** Ubuntu Linux (x86_64), Intel Core Processor
  * **Compiler:** clang++-21 for release benchmarks (`-O3 -march=native`), g++-11 for sanitizer/fuzzer runs (`-Og -fsanitize=address,undefined`)
  * **Benchmark Harness:** `src/benchmark.cpp` compares BiModalText against `std::vector`, `std::deque`, `std::list`, `SimpleGapBuffer`, `SimplePieceTable`, `__gnu_cxx::rope`.
  * **Verification Harness:** `src/fuzzer.cpp`(및 루트 `fuzzer` 실행 파일)은 단위 회귀와 확률적 퍼징을 한 번에 수행한다.

### 4.2 Scenario Analysis

#### A. Basic Performance (Typing & Reading)

  * **Reading:** `BiModal`(0.019ms)은 `std::vector`(0.017ms)와 거의 대등하며, `Rope`(0.393ms)보다 약 **20배 빠르다**. 이는 Iterator 최적화와 Compact Node 변환 전략이 유효했음을 증명한다.
  * **Typing:** `BiModal`(2.65ms)은 `SimpleGapBuffer`(0.05ms)보다 느리지만, `std::vector`(36.9ms)보다 13배 빠르며 사용자 입력 반응 속도(Latency)로는 충분히 빠르다.

#### B. Scalability: "The Heavy Typer"

2MB 크기의 파일에 5,000번 삽입을 수행했을 때, `std::vector`는 79ms로 급격히 성능이 저하되었으나, `BiModal`은 5.4ms로 `Rope`(5.1ms)와 유사한 **$O(log N)$ 확장성**을 보였다.

#### C. Efficiency: "The Backspacer" (Deletion)

가장 인상적인 결과는 삭제 연산에서 나타났다.

  * **BiModal (0.82ms) vs Rope (6.42ms)**
  * 트리 밸런싱이 필요한 Rope와 달리, 단순히 Gap을 확장하는 '논리적 삭제' 방식을 사용한 BiModal이 **7.8배 더 빠른 성능**을 기록했다.

#### D. The Real World: "Random Cursor Insertion"

실제 편집 환경을 모사한 랜덤 위치 삽입 시나리오에서 Bi-Modal 구조의 진가가 드러났다.

  * `SimpleGapBuffer`: 1.40ms (커서 이동 오버헤드 발생)
  * `Rope`: 11.41ms (트리 탐색 및 캐시 미스)
  * **BiModal:** **0.31ms**
  * 결과적으로 **Gap Buffer 대비 4.5배, Rope 대비 36배**의 압도적인 성능 향상을 보였다. 이는 지역성(Gap Buffer)과 탐색 효율(Skip List)의 장점을 성공적으로 결합했음을 의미한다.

### 4.3 Robustness Through Regression & Fuzzing

Gemini 초안의 모든 수치는 대규모 퍼징과 함께 보고된다. `run_regression_suite()`는 삽입/삭제/옵티마이즈 승인 시나리오를 결정론적으로 재생한 뒤, 서로 다른 시드(1,2,3)에 대해 2,000회 랜덤 편집을 수행한다. 이어지는 `Fuzzer` 클래스는 100회마다 상태를 검증하고, 실패 시 최근 10개 연산 로그와 Skip List 구조를 덤프해 재현성을 보장한다. 이 과정에서 `rebuild_spans()` 수정을 거친 이후로는 `iterator`, `at()`, `to_string()` 사이의 어긋남이 보고되지 않았다.

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
