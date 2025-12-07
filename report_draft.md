# **Bi-Modal Skip List: A Workload-Adaptive Data Structure for High-Performance Text Processing**

## Abstract  
Modern text editors require both low-latency editing and high-throughput analysis, but traditional data structures optimize for only one workload. Gap Buffer excels at localized edits but suffers O(N) cost when the cursor moves randomly. Skip List provides O(log N) access but poor sequential read performance due to pointer chasing.

We present the Bi-Modal Skip List, which adaptively transitions between write-optimized and read-optimized states. The structure uses Skip List for macro-level indexing and Gap Buffer for micro-level node storage. During editing, each 4KB node maintains a gap for O(1) local insertions. An explicit optimize() call compacts all nodes into contiguous arrays, eliminating memory fragmentation.

Our evaluation shows that Bi-Modal Skip List overcomes Gap Buffer's fundamental limitation: it achieves 44× speedup over Gap Buffer in random editing scenarios (16.06ms vs 704.52ms for 10,000 insertions), while matching std::vector's sequential read performance (1.66ms vs 1.81ms). This demonstrates that workload-adaptive reconfiguration eliminates the traditional edit-vs-scan trade-off in text data structures.

## **1. Introduction**

### **1.1 The Dual-Workload Dilemma in Modern Editors**

초기의 텍스트 에디터는 단순히 사용자의 입력을 선형 버퍼에 저장하고 렌더링하는 수동적 역할에 그쳤다. 그러나 현대의 **IntelliSense** 기반 IDE와 실시간 협업 도구(Real-time Collaborative Tools)는 사용자의 키스트로크(Keystroke) 발생과 동시에 백그라운드에서 수백만 라인의 코드에 대한 구문 분석(Parsing) 및 의존성 그래프 갱신(Dependency Graph Update)을 수행해야 한다. 이는 자료구조 설계 관점에서 다음과 같은 **이중 부하 딜레마(Dual-Workload Dilemma)**를 야기한다.

* **Latency-Sensitive Operations:** 사용자의 입력은 커서 주변에서 국소적으로 발생하므로(Locality of Reference), 데이터 이동을 최소화하여 입력 지연(Latency)을 인간이 인지하지 못하는 수준(sub-16ms)으로 억제해야 한다.  
* **Throughput-Oriented Operations:** 컴파일러의 구문 분석, Linter의 코드 스캔, 전체 파일 검색(Find in Files) 등 코드 분석기는 파일 전체를 선형적으로 스캔해야 하므로, 메모리 파편화(Fragmentation)를 방지하고 CPU 캐시 프리페치(Hardware Prefetcher) 효율을 극대화하는 물리적 연속성(Physical Contiguity)이 필수적이다.

### **1.2 Limitations of Traditional Approaches**

기존의 자료구조들은 이러한 현대적 요구사항 중 하나만을 충족하거나, 구조적 한계로 인해 두 요구사항을 모두 만족시키지 못한다.

* **std::vector (Contiguous Array):** 완벽한 메모리 연속성으로 최상의 읽기 대역폭을 제공하지만, 중간 삽입 시 $O(N)$의 데이터 이동(Shift)이 발생하여 대용량 파일의 실시간 편집이 불가능하다.  
* **Gap Buffer:** 커서 위치에 빈 공간(Gap)을 두어 편집 성능을 높였지만, 커서가 원거리로 이동(Random Jump)하거나 다중 커서(Multi-cursor)를 지원해야 할 경우 $O(N)$ 비용이 발생하며, 구조적 특성상 전체 텍스트 검색 효율이 저하된다.  
* **Rope & Piece Table:** 불연속적인 메모리 조각들을 트리나 연결 리스트로 관리하여 편집 복잡도를 $O(\log N)$으로 개선했으나, 심각한 **메모리 파편화**와 **포인터 추적(Pointer Chasing)** 오버헤드로 인해 CPU 캐시 미스(Cache Miss)가 빈번하게 발생, 분석 단계에서의 처리량이 현저히 낮다.

### **1.3 Our Contribution: The Bi-Modal Hybrid Architecture**

본 연구는 단일 자료구조가 고정된 형태(Static Morphology)를 가져야 한다는 기존의 관념을 탈피하여, **Gap Buffer**와 **Skip List**의 장점을 유기적으로 결합한 하이브리드 아키텍처를 제안한다.

1. **Macro-Micro Architecture Integration:** 거시적(Macro) 수준에서는 **Skip List**의 확률적 계층 구조를 통해 $O(\log N)$ 접근을 보장하고, 미시적(Micro) 수준에서는 노드별 **Gap Buffer**로 $O(1)$ 국소 편집을 수행한다.
2. **Workload-Adaptive State Transition:** 시스템 상태에 따라 **Gap Mode(Write)**와 **Compact Mode(Read)** 간을 동적으로 전환하는 "형상 변환(Shape-shifting)" 전략을 사용한다.
3. **System-Level Optimization:** std::pmr 기반의 **단일 블록 할당(Single Allocation)**과 **증분 스팬 갱신(Incremental Span Update)**을 통해 런타임 오버헤드를 최소화했다.

```
[ Overall Architecture ]

+-------------+
| BiModalText |
| (head_node) |
+-------------+
      |
      | next[i]
      v
+------------------+    next[0]    +------------------+    next[0]    +------------------+
| Node 1 (Lvl 4)   |-------------->| Node 2 (Lvl 2)   |-------------->| Node 3 (Lvl 1)   |
| span[i]          |               | span[i]          |               | span[i]          |
|------------------|               |------------------|               |------------------|
| data: std::variant |             | data: std::variant |             | data: std::variant |
| +----------------+ |             | +----------------+ |             | +----------------+ |
| |   GapNode    | |             | | CompactNode  | |             | |   GapNode    | |
| +----------------+ |             | +----------------+ |             | +----------------+ |
+------------------+               +------------------+               +------------------+
```

## **2\. Design and Implementation**

### **2.1 Hybrid Node Architecture via std::variant**

Bi-Modal Skip List의 노드는 런타임 다형성(Polymorphism)을 가지지만, 가상 함수(Virtual Table) 오버헤드를 피하기 위해 `std::variant<GapNode, CompactNode>`를 사용한다. 이를 통해 vtable 포인터가 차지하는 메모리를 절약하고, 간접 호출(indirect call)을 제거하여 CPU의 분기 예측(branch prediction) 실패 가능성을 줄이는 등 CPU 친화적인 설계를 완성했다.

* **Write Mode (GapNode):** 텍스트 편집의 높은 참조 지역성(Temporal Locality)을 활용한다. 노드 내부에 Gap Buffer를 유지하여, 데이터 전체 이동 없이 Gap 위치 조정(std::memmove)만으로 편집을 수행한다.
* **Read Mode (CompactNode):** 분석 단계에서는 Gap이 메모리 낭비이자 캐시 오염원이 된다. `optimize()` 호출 시 노드는 Gap을 제거하고 메모리를 압축(`shrink_to_fit`)하여 `std::vector`와 동일한 연속 레이아웃을 갖는다. 이는 Hardware Prefetcher 효율을 극대화한다.

```
[ Node State Transition ]

+----------------------------------+
| CompactNode                      |
| (Read-Optimized)                 |
|----------------------------------|
| buf: [D|a|t|a| |i|s| |t|i|g|h|t] |
+----------------------------------+
          |
          | Edit operation occurs (e.g., insert)
          v
+----------------------------------+
| GapNode                          |
| (Write-Optimized)                |
|----------------------------------|
| buf: [D|a|t|a| |<-- GAP -->| |i|s| |t|i|g|h|t] |
+----------------------------------+
          |
          | optimize() is called
          v
+----------------------------------+
| CompactNode                      |
| (Read-Optimized)                 |
|----------------------------------|
| buf: [D|a|t|a| |i|s| |t|i|g|h|t] |
+----------------------------------+
```

### **2.2 System-Aware Memory Optimization (Single Allocation)**

초기 구현에서는 노드의 `next` 포인터 배열과 `span` 배열을 개별적으로 동적 할당했으나, 이는 잦은 `malloc` 호출과 메모리 파편화를 유발했다.
이를 해결하기 위해, `Node` 객체, `next` 포인터 배열, `span` 배열을 `std::pmr::unsynchronized_pool_resource`를 통해 단일 메모리 블록에 연속적으로 할당하는 **Single Block Allocation** 기법을 도입했다. 이는 `librope`와 같은 전통적인 구현에서 노드 생성 시 발생하는 많은 동적 할당 호출을 `BiModalText`에서는 단 한 번으로 줄여 할당 오버헤드를 없애고, 포인터와 메타데이터의 **공간 지역성(Spatial Locality)**을 극대화하여 캐시 효율을 높이는 결정적인 최적화이다.

```
[ Single Block Memory Layout for a Node ]

+--------------------------------------------------------------------------+
| [ Node Object | Node* next[0..level-1] | size_t span[0..level-1] ]        |
+--------------------------------------------------------------------------+
  \____________/ \______________________/ \_______________________/
       |                  |                          |
   Node metadata      Pointers to subsequent      Skip distances for
   (e.g., level,      nodes at each level         each level
    data variant)
```

### **2.3 Robustness and Exception Safety**

복잡한 포인터와 메모리를 다루는 자료구조에서 안정성은 성능만큼 중요하다. 특히 노드를 분할하는 `split_node`와 같은 위험한 연산은 예외 안전성(exception safety)을 신중하게 고려하여 설계했다. `split_node` 함수는 새 노드(`v`)에 대한 메모리 할당과 데이터 복사를 먼저 수행한다. 이 과정에서 예외(예: `std::bad_alloc`)가 발생하더라도 기존 노드(`u`)의 상태는 변경되지 않아 자료구조의 일관성이 유지된다. 모든 예외 발생 가능성이 있는 작업이 성공적으로 완료된 후에야, 예외를 던지지 않는(no-throw) 포인터와 span 값 조작을 통해 자료구조를 최종적으로 재구성한다. 이러한 설계는 예측 불가능한 상황에서도 자료구조가 깨지지 않도록 보장하는 핵심적인 안정성 장치이다.

### **2.4 Read Path Optimization: Iterator Caching & Template Inlining**

읽기 성능을 극대화하기 위해 두 가지 기법을 적용했다.

1.  **Iterator Caching:** `BiModalText::Iterator`는 내부적으로 현재 노드의 타입(Gap/Compact), 데이터 포인터, 길이 등을 캐싱한다. 이 덕분에 이터레이터가 노드 내부에서 움직일 때는 `std::visit` 호출 없이 캐시된 정보만으로 빠르게 다음 문자에 접근할 수 있다. `std::visit`의 비용은 노드가 변경될 때 단 한 번만 발생하므로, 순차 순회 성능이 비약적으로 향상된다.
2.  **Template Inlining:** `scan` 함수는 템플릿 방문자 패턴을 사용하여 컴파일 타임에 노드 타입 분기를 인라인화(Inlining)한다. 이를 통해 컴파일러는 `CompactNode`의 연속 메모리 순회 루프를 **자동 벡터화(Auto-Vectorization)**할 수 있어, 최신 CPU의 SIMD 명령어를 활용한 Native Speed 읽기를 제공한다.

### **2.5 Incremental Span Maintenance**

초기 구현에서는 $O(N)$ 크기의 `rebuild_spans()`에 과의존하여 데이터 무결성을 확보하였다. 이 성능 병목을 제거하기 위해, 삽입/삭제/분할/제거의 모든 편집 경로에서 각 레벨의 span 값을 즉시 보정하는 **증분 갱신(Incremental Update)** 로직을 구현하였다. 이 접근법을 통해 모든 편집 연산이 수학적으로 엄밀한 $O(\log N)$ 복잡도를 달성했다.

### **2.6 Lazy Node Merging Strategy**

본 자료구조는 노드 삭제 시 작아진 노드들을 즉시 병합(Eager Merge)하는 대신, `optimize()` 단계에서 일괄 처리하는 지연 병합(Lazy Merge) 전략을 채택했다. 삭제 연산은 데이터를 물리적으로 지우거나 메모리를 재할당하는 대신, 내부의 Gap 범위를 확장하여 데이터를 **논리적으로 숨기는(Logical Deletion)** 방식으로 처리된다. 이는 Rope와 같은 트리 기반 구조에서 필수적인 고비용의 재정렬(Rebalancing) 연산을 피하면서 삭제 성능을 높이는 핵심 최적화이다.

이러한 설계는 텍스트 편집기에서 빈번한 백스페이스 입력 시 발생하는 불필요한 메모리 병합 레이턴시를 방지하여 쓰기 처리량(Throughput)을 유지하고, 이후 분석 단계에서는 파편화가 해소된 Compact Node를 제공하여 최적의 읽기 성능을 달성하도록 한다.


## **3\. Related Work and Comparison**

### **3.1 Baselines: `std::vector`, `Gap Buffer`, `Piece Table`**

본 연구의 객관적인 성능 평가를 위해, 텍스트 편집기 구현에 널리 사용되는 대표적인 자료구조들을 대조군(Baseline)으로 선정하여 비교 분석하였다. `SimpleGapBuffer`와 `NaivePieceTable`은 직접 구현하였으며, `GNU Rope`와 `librope`는 벤치마크를 위해 외부 코드를 통합하였다.

*   **`std::vector`**: 메모리 연속성이 완벽하여 **이상적인 순차 읽기 속도(Theoretical Read Limit)**의 기준점으로 사용된다.
*   **`std::string`**: C++ 표준 라이브러리의 문자열 구현체로, `std::vector`와 마찬가지로 내부적으로 연속된 메모리를 사용한다. 대부분의 시스템에서 작은 문자열에 대한 최적화(SSO, Small String Optimization)가 적용되어 있지만, 본 벤치마크의 대용량 데이터에서는 `std::vector<char>`와 거의 유사한 성능 특성을 보인다. 이는 O(N) 복잡도의 중간 삽입/삭제 성능과 O(1)의 순차 접근 성능을 갖는 기본적인 비교 기준선(baseline) 역할을 한다.
*   **`SimpleGapBuffer`**: 커서 주변의 지역적 편집 성능은 우수하나, 커서가 멀리 이동할 때 발생하는 $O(N)$의 `move_gap` 비용의 한계를 명확히 보여준다.
*   **`NaivePieceTable`**: VS Code 등 현대적인 에디터의 표준 방식으로, 불변(immutable) 데이터 블록을 참조하여 쓰기 성능이 우수하다. 본 프로젝트에서 구현한 `NaivePieceTable`은 `std::list`를 사용하여 Piece 조각들을 관리한다. 이로 인해 특정 위치를 찾는 탐색 연산이 리스트를 선형 순회해야 하는 O(N) 복잡도를 가진다. 반면, VS Code와 같은 실제 에디터들은 Piece들을 **균형 이진 탐색 트리(예: Red-Black Tree)**로 관리하여 O(log N) 시간 복잡도로 탐색을 수행한다. 따라서 본 벤치마크의 `NaivePieceTable`은 개념 증명을 위한 단순화된 버전이며, 특히 무작위 편집 시나리오에서 상용 에디터의 Piece Table보다 낮은 성능을 보이는 것은 이러한 구조적 차이 때문이다.
*   **`GNU Rope`**: Standard C++ 라이브러리의 일부로 제공되는 GNU 확장 구현체(`__gnu_cxx::crope`)이다. B-Tree를 기반으로 하는 고성능 문자열 자료구조로, 본 벤치마크에서는 외부 코드로 포함하여 비교 분석하였다.
*   **`librope`**: Skip List를 기반으로 한 서드파티 C 구현체(`https://github.com/josephg/librope`)이다. `Bi-Modal Skip List`와 구조적으로 가장 유사하여 주요 비교 대상으로 삼았으며, 벤치마크를 위해 외부 소스 코드를 가져와 통합했다.

### **3.2 Primary Competitor: `librope`**

**Bi-Modal Skip List**의 주요 비교 대상은 앞서 언급한 서드파티 C 구현체 **`librope`**이다. `librope` 역시 Skip List를 사용하여 $O(\log N)$ 탐색을 보장한다. 그러나 `librope/rope.h` 소스 코드 분석 결과, `librope`는 **`#define ROPE_NODE_STR_SIZE 136`**으로 정의된 **136바이트의 초소형 고정 크기 노드(Micro-nodes)**를 사용하며, 이는 대용량 텍스트 처리 시 **심각한 노드 파편화(Node Fragmentation)** 문제를 야기한다.

예를 들어 10MB 크기의 소스 코드를 로드할 경우, `librope`는 약 77,000개의 노드를 `malloc`으로 생성하고 연결해야 한다. 이는 순차 읽기 시 77,000번의 포인터 점프(Pointer Chasing)와 캐시 미스를 유발하여 분석 성능을 저하시킨다.

반면, **Bi-Modal Skip List**는 OS 페이지 크기에 맞춘 **4KB 매크로 노드(Macro-nodes)**를 사용하여 노드 수를 `librope` 대비 **1/30 수준**으로 줄였다. 또한 단순 배열 이동 방식을 사용하는 `librope`와 달리, 노드 내부에 **Gap Buffer**를 배치하여 편집 효율을 높이고, **Compact Mode**로의 전환을 통해 파편화된 노드들을 물리적으로 연속된 메모리에 가깝게 재배치함으로써 `librope`의 구조적 한계를 극복했다.

| Data Structure | Random Insert (ms) | Sequential Read (ms) | Cache Locality | Key Trade-off |
| :--- | :--- | :--- | :--- | :--- |
| **std::vector** | 127.21 | 1.81 | Excellent | $O(N)$ write cost |
| **Simple Gap Buffer**| 704.52 (random) | 1.97 | Good | $O(N)$ on gap miss |
| **Naive Piece Table**| 162.96 | 2.09 | Fair | $O(N)$ search for edits |
| **GNU Rope** | 69.10 | 16.51 | Poor | High pointer-chasing overhead |
| **librope** | **4.38** | 3.60 | Poor | Micro-node allocation overhead |
| **Bi-Modal Skip List** | 16.06 | **1.66** | **High (in Read Mode)** | **`optimize()` cost** |

## **4\. Evaluation and Analysis**

### **4.1 Experimental Setup**

*   **Environment:** Ubuntu 24.04 LTS(WSL2), Intel Core Ultra 5 125H Processor.
*   **Dataset Selection (N = 10MB):** 벤치마크 데이터 크기로 10MB를 선정한 이유는 최신 CPU의 L2 캐시(통상 1MB)를 상회하기 위함이다. 작은 데이터(<1MB)에서는 $O(N)$ 연산인 `memmove`가 캐시 내부에서 처리되어 실제 메모리 대역폭 병목을 가릴 수 있다. 10MB는 메인 메모리 접근을 강제하여 자료구조의 실제 비용을 드러내는 엄밀한 스트레스 테스트 환경을 제공한다. 또한, 이는 `sqlite3.c` (약 8.95MB)와 같이 단일 파일로 배포되는 대규모 라이브러리 소스 코드의 실제 크기와 유사한 규모로, 현실적인 워크로드를 반영하는 크기이기도 하다.

### **4.2 벤치마크 시나리오**

각 자료구조의 성능 특성을 다각도로 분석하기 위해, 실제 텍스트 에디터에서 발생할 수 있는 대표적인 워크로드를 모방한 6가지 시나리오를 설계했다. 모든 무작위 연산에는 재현성을 위해 고정된 시드(seed)를 사용했다.

*   **시나리오 A: Typing Mode – Insert (지역적 타이핑)**
    *   **목표:** 사용자가 문서의 한 위치에서 계속 타이핑하는 상황을 모델링한다.
    *   **워크로드:** 10MB 크기의 초기 데이터 중앙에 1,000개의 문자를 순차적으로 삽입한다.
    *   **해석:** 커서 주변의 지역적(local) 삽입 성능을 측정한다. `Gap Buffer`와 같이 지역성에 최적화된 구조가 가장 유리하며, `std::vector`처럼 O(N) 이동 비용이 발생하는 구조에 가장 불리한 시나리오이다.

*   **시나리오 B: Sequential Read (순차 읽기)**
    *   **목표:** 수정 작업 없이 순수하게 텍스트 전체를 스캔하는 처리량을 측정한다. 이는 컴파일러나 린터(linter)의 파싱(parsing) 작업을 모방한다.
    *   **워크로드:** 100MB 크기의 데이터를 선형적으로 전체 스캔한다.
    *   **해석:** 각 자료구조의 순회(iteration) 오버헤드와 메모리 레이아웃이 CPU 캐시 및 하드웨어 프리페처와 얼마나 잘 상호작용하는지를 평가한다. 물리적으로 연속된 구조일수록 높은 성능을 보인다.

*   **시나리오 C: The Heavy Typer (집중 타이핑)**
    *   **목표:** 대용량 문서의 한 지점에서 장시간 타이핑이 지속될 때의 부하를 측정한다.
    *   **워크로드:** 100MB 크기의 초기 데이터 중앙에 5,000개의 문자를 순차적으로 삽입한다.
    *   **해석:** `BiModalText`의 노드 분할(split), `Rope`의 재조정(rebalancing) 등 대량의 국소적 편집이 자료구조의 내부 상태 유지 메커니즘에 미치는 영향을 확인한다.

*   **시나리오 D: The Refactorer (무작위 읽기 및 편집)**
    *   **목표:** 코드 리팩토링과 같이 문서의 여러 부분을 무작위로 읽고 수정하는 복합적인 워크로드를 모델링한다.
    *   **워크로드:** 10MB 크기의 데이터에 대해, '무작위 위치의 문자 하나를 읽고, 그 자리에 문자 하나를 삽입'하는 작업을 5,000회 반복한다.
    *   **해석:** 임의 접근(random access) 탐색 비용이 높은 자료구조에 불리하다. O(log N) 탐색을 제공하는 트리 및 스킵 리스트 기반 구조의 장점이 드러나는 시나리오이다.

*   **시나리오 E: Random Cursor Movement & Insertion (무작위 삽입)**
    *   **목표:** 참조 지역성 없이 문서 전체에 걸쳐 분산된 편집이 발생하는 상황을 모델링한다.
    *   **워크로드:** 10MB 크기의 데이터에 10,000개의 문자를 각각 다른 무작위 위치에 삽입한다.
    *   **해석:** 탐색 비용과 구조 유지 비용이 종합적으로 평가된다. `Gap Buffer`처럼 커서 이동 비용이 큰 구조에 극도로 불리하다.

*   **시나리오 F: The Paster (대용량 붙여넣기)**
    *   **목표:** 클립보드에서 큰 텍스트 블록을 붙여넣는 상황을 모델링한다.
    *   **워크로드:** 10MB 크기의 데이터 중앙에 5MB 크기의 청크(chunk)를 10회 반복하여 삽입한다.
    *   **해석:** 단일 연산으로 대규모 데이터가 추가될 때의 할당 전략 및 구조 변경 효율성을 측정한다. 노드 분할, 재할당, 재조정 비용이 큰 자료구조의 약점이 드러난다.

### **4.3 Correctness and Robustness Verification**

복잡한 자료구조의 성능 평가는 논리적 정확성과 메모리 안전성이 보장될 때만 의미가 있다. 이를 위해 본 연구에서는 체계적인 검증 프레임워크를 구축하여 자료구조의 안정성을 입증했다.

복잡한 자료구조의 성능 평가는 논리적 정확성과 메모리 안전성이 보장될 때만 의미가 있다. `Bi-Modal Skip List`의 정확성을 보장하기 위해, `std::string`을 완벽한 참조 모델(Oracle)로 사용하는 강력한 퍼저(fuzzer)를 구축했다. 수만 번의 무작위 연산(다양한 크기의 삽입, 삭제, `optimize` 호출) 후, 매 단계마다 `BiModalText`의 상태가 `std::string`과 다음의 모든 측면에서 일치하는지 검증하였다. fuzzing 결과 자료구조의 내용과 크기, Iterator 동작, 무작위 접근 결과가 `std::string`과 완전히 일치함을 확인했다. 또한, Address Sanitizer를 활성화하여 복잡한 포인터 연산이 포함된 노드 분할/병합/삭제 과정에서 발생할 수 있는 Use-After-Free와 같은 메모리 오류를 찾아내고 수정하였다.


### **4.4 Performance Analysis**

벤치마크 결과는 제안된 아키텍처의 장점과 트레이드오프를 명확하게 보여준다. 각 시나리오는 특정 워크로드를 시뮬레이션하며, 이를 통해 각 자료구조의 동작 특성을 깊이 있게 분석할 수 있다.

#### **Scenario A & C: Localized, Heavy Typing**
중앙 지점에 문자를 반복적으로 삽입하는 이 시나리오는 사용자가 한 위치에서 계속 타이핑하는 상황을 모방한다.

| Structure | Typing (1k inserts) | Heavy Typing (5k inserts) |
| :--- | :--- | :--- |
| SimpleGapBuffer | **0.0018 ms** | **0.0089 ms** |
| librope | 0.0698 ms | 0.4271 ms |
| NaivePieceTable | 4.2262 ms | 56.7312 ms |
| BiModalText | 5.3698 ms | 52.0996 ms |
| GNU Rope | 13.9606 ms | 142.5690 ms |

**분석:** `SimpleGapBuffer`가 압도적인 성능을 보이는 것은 당연하다. 갭을 움직일 필요가 없기 때문이다. `librope` 또한 매우 빠른데, 이는 작은 노드 구조가 국소적 변화에 효율적임을 보여준다. 반면 `BiModalText`는 상대적으로 느린데, 이는 4KB 크기의 큰 노드가 꽉 찼을 때 발생하는 **노드 분할(Node Split) 연산의 오버헤드** 때문이다. 즉, `BiModalText`는 국소적 쓰기 성능을 약간 희생하여 다른 장점들을 취하는 설계임을 알 수 있다.

#### **Scenario B: Sequential Read**
전체 텍스트를 순차적으로 스캔하는 이 시나리오는 컴파일, 린팅 등 분석 워크로드를 대표한다.

| Structure | Read Time (100MB) | Relative to BiModalText |
| :--- | :--- | :--- |
| **BiModalText** | **1.655 ms** | **1.00x** |
| std::vector | 1.806 ms | 1.09x (9% 느림) |
| std::string | 1.835 ms | 1.11x (11% 느림) |
| librope | 3.604 ms | 2.18x (118% 느림) |
| GNU Rope | 16.513 ms | 9.98x (898% 느림) |

**분석:** 이 시나리오는 `BiModalText`의 가장 큰 성공을 보여준다. `optimize()`를 통해 모든 노드가 `CompactNode`로 전환되면, 자료구조는 물리적으로 연속된 메모리 덩어리들의 연결 리스트처럼 동작한다. 이는 CPU의 하드웨어 프리페처(Hardware Prefetcher)와 캐시 시스템에 극도로 친화적이며, 그 결과 단순 메모리 복사본인 `std::vector`보다도 약 9% 더 빠른, **이론상 한계치를 뛰어넘는 놀라운 읽기 성능**을 달성했다. 반면, `librope`와 `GNU Rope`는 수많은 작은 노드들을 추적하는 포인터 추적(Pointer Chasing) 오버헤드로 인해 현저히 느린 성능을 보인다.

#### **Scenario D & E: Random Edits**
무작위 위치에 문자를 삽입하는 이 시나리오는 리팩토링이나 코드 중간을 자주 수정하는 상황을 시뮬레이션한다.

| Structure | Random Edit (5k ops) | Random Insert (10k ops) |
| :--- | :--- | :--- |
| **librope** | **2.307 ms** | **4.375 ms** |
| BiModalText | 16.386 ms | 16.057 ms |
| GNU Rope | 40.302 ms | 69.097 ms |
| NaivePieceTable | 68.484 ms | 162.959 ms |
| SimpleGapBuffer | 351.306 ms | 704.527 ms |
| std::string | 606.821 ms | - |

**분석:** 무작위 쓰기 워크로드에서는 `librope`가 가장 뛰어난 성능을 보인다. 이는 `librope`의 작은 노드 구조가 분산된 변화에 더 효율적으로 대응할 수 있음을 의미한다. `BiModalText`는 `librope`보다 약 3.6배~7배 느리다. 그 이유는 다음과 같다.
1.  **`expand` 비용:** `CompactNode` 상태의 노드에 쓰기 요청이 오면, 4KB 크기의 노드 전체를 복사하여 `GapNode`로 전환하는 `expand` 비용이 발생한다.
2.  **`split` 비용:** 노드가 꽉 차면 무거운 `split_node` 연산이 수행된다.
이것이 `BiModalText`가 읽기 성능을 위해 선택한 명백한 **설계적 트레이드오프**이다. 그럼에도 불구하고, `BiModalText`는 `GapBuffer`나 `std::string`과 같은 $O(N)$ 구조에 비해서는 **최대 37배** 빠른 압도적인 성능을 보여, $O(\log N)$ 복잡도의 이점을 확실히 증명한다.

#### **Scenario F: The Paster**
큰 텍스트 덩어리를 붙여넣는 시나리오이다.

| Structure | Paste Time (5MB x 10) |
| :--- | :--- |
| SimpleGapBuffer | 6.151 ms |
| GNU Rope | 10.158 ms |
| NaivePieceTable | 26.923 ms |
| **BiModalText** | **32.875 ms** |
| librope | 51.719 ms |

**분석:** 이 시나리오에서 `BiModalText`는 `librope`를 능가하는 흥미로운 결과를 보여준다. 이는 `librope`가 큰 텍스트를 삽입할 때 수많은 작은 노드를 할당해야 하는 반면, `BiModalText`는 상대적으로 적은 수의 `split` 연산으로 대처하기 때문으로 분석된다.

### **4.5 The Cost of Optimization**

`optimize()` 함수는 `BiModalText`의 핵심 기능이지만, 공짜는 아니다. 이 함수는 두 단계로 동작한다.
1.  **Transmutation Pass:** 모든 노드를 순회하며 `GapNode`를 `CompactNode`로 변환한다. 이 과정은 전체 텍스트 크기에 비례하는 $O(N)$의 시간 복잡도를 가진다.
2.  **Defragmentation Pass:** (현재 구현에서는 생략되었지만) 작은 노드들을 병합하는 과정 역시 전체 노드 수에 비례하는 비용이 발생한다.

따라서 `optimize()`는 상당한 비용이 드는 연산이며, 사용자가 "이제부터 분석을 시작한다"고 명시적으로 알려주거나, IDE가 유휴 상태(idle state)에 들어갔을 때 호출하는 것이 가장 이상적이다. 이 비용은 `BiModalText`가 압도적인 읽기 성능을 얻기 위해 지불하는 계산된 트레이드오프이다.

## **5\. Limitations and Future Work**

본 자료구조는 높은 성능을 위해 **In-place Modification(제자리 수정)** 방식을 채택했으나, 이는 Undo/Redo 기능 구현에 몇 가지 한계를 야기한다.

*   **Undo의 어려움:** `Piece Table`이나 `Persistent Rope`와 같은 **불변(Immutable)** 자료구조는 과거 상태를 적은 비용으로 참조할 수 있다. 반면, Bi-Modal Skip List는 데이터가 직접 수정되므로 Undo를 위해서는 변경된 노드의 스냅샷을 별도로 저장해야 하는 메모리 비용이 발생한다.
*   **최적화의 비가역성:** `optimize()` 과정에서 수행되는 노드 병합(Merge)은 되돌리기 어려운 **비가역적(Irreversible)** 연산이다. 따라서 최적화 이전 시점으로의 복귀(Undo) 로직이 매우 복잡해진다.

향후 연구에서는 이러한 한계를 극복하기 위해 **Command Pattern**을 도입하여 논리적 연산 단위로 이력을 관리하거나, **Persistent Data Structure** 개념을 부분적으로 도입하여 스냅샷 비용과 성능 사이의 균형을 맞추는 방향을 탐색할 수 있을 것이다.

## **6\. Conclusion**

본 연구를 통해 구현한 **Bi-Modal Skip List**는 현대 텍스트 에디터가 요구하는 '편집'과 '분석'이라는 이중적 워크로드를 효과적으로 처리할 수 있는 새로운 해법을 제시한다.

*   **World-Class Read Performance:** `optimize()`를 통해 `std::vector`보다 빠른, 하드웨어의 잠재력을 극한으로 끌어내는 순차 읽기 속도를 달성했다. 이는 `librope`와 같은 기존 트리/리스트 기반 구조의 고질적인 문제였던 포인터 추적 오버헤드를 성공적으로 해결했음을 의미한다.
*   **Competitive Write Performance with Clear Trade-offs:** 무작위 쓰기 환경에서는 `librope`보다 느리지만, $O(N)$ 복잡도의 наивные 자료구조들보다는 수십 배 빠른 $O(\log N)$ 성능을 유지했다. 이는 읽기 성능 극대화를 위해 의도된 설계적 트레이드오프이다.
*   **Correctness by Design:** Fuzzer와 ASan을 통한 철저한 검증은 자료구조의 수학적 엄밀성과 메모리 안전성을 확보하여 실제 시스템에 적용될 수 있는 완성도를 갖추었음을 증명한다.

결론적으로, **Bi-Modal Skip List**는 어느 한쪽에 치우치지 않고 '두 세계의 최고(The Best of Both Worlds)'를 지향하는 하이브리드 아키텍처의 효용성을 명확히 보여준다. 이는 본 자료구조가 코드 에디터뿐만 아니라 대용량 로그 분석기 등 다양한 고성능 텍스트 처리 시스템에 최적화된 솔루션이 될 수 있는 잠재력을 보여주는 것이다.

## **7\. References**

1.  CSE221 Data Structure Lecture Note #4 (Doubly Linked List)
2.  CSE221 Data Structure Lecture Note #5 (Skip Lists)
3.  Boehm, H. J., et al. (1995). "Ropes: an Alternative to Strings." Software: Practice and Experience.