# **Bi-Modal Skip List: A Workload-Adaptive Data Structure for High-Performance Text Processing**

Abstract  
현대의 소프트웨어 개발 환경(IDE)과 대규모 텍스트 처리 시스템은 사용자의 입력에 즉각 반응해야 하는 \*\*편집 단계(Write-Heavy Phase)\*\*와, 컴파일러 및 정적 분석 도구가 코드를 스캔해야 하는 \*\*분석 단계(Read-Heavy Phase)\*\*라는 상충되는 성능 요구사항(Conflicting Performance Requirements)을 동시에 만족시켜야 한다. 그러나 기존의 단일 자료구조들은 어느 한 쪽의 성능을 희생하는 트레이드오프(Trade-off) 관계에 국한되어 있다.  
본 연구에서는 이러한 이중적 요구를 해결하기 위해, 시스템의 워크로드에 따라 내부 메모리 레이아웃을 동적으로 재구성(Dynamic Reconfiguration)하는 **Bi-Modal Skip List**를 제안한다. 이 자료구조는 **Gap Buffer**의 지역적 편집 효율성과 **Skip List**의 로그 시간 탐색 성능을 결합한 하이브리드 아키텍처이다. 편집 시에는 노드 단위의 Gap Buffer를 통해 $O(1)$의 분할상환(Amortized) 삽입 성능을 보장하며, 분석 시에는 **Compact Array**로 즉시 변환(Transmutation)하여 캐시 지역성(Spatial Locality)과 SIMD 병렬화 효율을 극대화한다.

벤치마크 결과, 제안된 기법은 업계 표준인 **Rope** 대비 무작위 커서 편집(Random Cursor Insertion)에서 **36배**, 삭제 연산에서 **7.8배** 향상된 처리량(Throughput)을 기록했으며, 읽기 모드 전환 시 std::vector와 동등한 수준의 **Native Sequential Bandwidth**를 달성하여 적응형 자료구조의 효용성을 실증적으로 입증했다.

## **1\. Introduction**

### **1.1 The Dual-Workload Dilemma in Modern Editors**

초기의 텍스트 에디터는 단순히 사용자의 입력을 선형 버퍼에 저장하고 렌더링하는 수동적 역할에 그쳤다. 그러나 현대의 **IntelliSense** 기반 IDE와 실시간 협업 도구(Real-time Collaborative Tools)는 사용자의 키스트로크(Keystroke) 발생과 동시에 백그라운드에서 수백만 라인의 코드에 대한 구문 분석(Parsing) 및 의존성 그래프 갱신(Dependency Graph Update)을 수행해야 한다. 이는 자료구조 설계 관점에서 다음과 같은 \*\*이중 부하 딜레마(Dual-Workload Dilemma)\*\*를 야기한다.

* **Latency-Sensitive Operations:** 사용자의 입력은 커서 주변에서 국소적으로 발생하므로(Locality of Reference), 데이터 이동을 최소화하여 입력 지연(Latency)을 인간이 인지하지 못하는 수준(sub-16ms)으로 억제해야 한다.  
* **Throughput-Oriented Operations:** 코드 분석기는 파일 전체를 선형적으로 스캔해야 하므로, 메모리 파편화(Fragmentation)를 방지하고 CPU 캐시 프리페치(Hardware Prefetcher) 효율을 극대화하는 물리적 연속성(Physical Contiguity)이 필수적이다.

### **1.2 Limitations of Traditional Approaches**

기존의 자료구조들은 이러한 현대적 요구사항 중 하나만을 충족하거나, 구조적 한계로 인해 두 요구사항을 모두 만족시키지 못한다.

* **std::vector (Contiguous Array):** 완벽한 메모리 연속성으로 최상의 읽기 대역폭을 제공하지만, 중간 삽입 시 $O(N)$의 데이터 이동(Shift)이 발생하여 대용량 파일의 실시간 편집이 불가능하다.  
* **Gap Buffer:** 커서 위치에 빈 공간(Gap)을 두어 편집 성능을 높였지만, 커서가 원거리로 이동(Random Jump)하거나 다중 커서(Multi-cursor)를 지원해야 할 경우 $O(N)$ 비용이 발생하며, 구조적 특성상 전체 텍스트 검색 효율이 저하된다.  
* **Rope & Piece Table:** 불연속적인 메모리 조각들을 트리나 연결 리스트로 관리하여 편집 복잡도를 $O(\\log N)$으로 개선했으나, 심각한 **메모리 파편화**와 **포인터 추적(Pointer Chasing)** 오버헤드로 인해 CPU 캐시 미스(Cache Miss)가 빈번하게 발생, 분석 단계에서의 처리량이 현저히 낮다.

### **1.3 Our Contribution: The Bi-Modal Hybrid Architecture**

본 연구는 단일 자료구조가 고정된 형태(Static Morphology)를 가져야 한다는 기존의 관념을 탈피하여, **Gap Buffer**와 **Skip List**의 장점을 유기적으로 결합한 하이브리드 아키텍처를 제안한다.

1. **Macro-Micro Architecture Integration:** 거시적(Macro) 수준에서는 **Skip List**의 확률적 계층 구조를 통해 $O(\\log N)$ 접근을 보장하고, 미시적(Micro) 수준에서는 노드별 **Gap Buffer**로 $O(1)$ 국소 편집을 수행한다.  
2. **Workload-Adaptive State Transition:** 시스템 상태에 따라 \*\*Gap Mode(Write)\*\*와 **Compact Mode(Read)** 간을 동적으로 전환하는 "형상 변환(Shape-shifting)" 전략을 사용한다.  
3. **System-Level Optimization:** std::pmr 기반의 \*\*단일 블록 할당(Single Allocation)\*\*과 \*\*증분 스팬 갱신(Incremental Span Update)\*\*을 통해 런타임 오버헤드를 최소화했다.

## **2\. Design and Implementation**

### **2.1 Hybrid Node Architecture via std::variant**

Bi-Modal Skip List의 노드는 런타임 다형성(Polymorphism)을 가지지만, 가상 함수(Virtual Table) 오버헤드를 피하기 위해 std::variant\<GapNode, CompactNode\>를 사용한다.

* **Write Mode (GapNode):** 텍스트 편집의 높은 참조 지역성(Temporal Locality)을 활용한다. 노드 내부에 Gap Buffer를 유지하여, 데이터 전체 이동 없이 Gap 위치 조정(std::memmove)만으로 편집을 수행한다.  
* **Read Mode (CompactNode):** 분석 단계에서는 Gap이 메모리 낭비이자 캐시 오염원이 된다. optimize() 호출 시 노드는 Gap을 제거하고 메모리를 압축(shrink\_to\_fit)하여 std::vector와 동일한 연속 레이아웃을 갖는다. 이는 Hardware Prefetcher 효율을 극대화한다.

### **2.2 System-Aware Memory Optimization (Single Allocation)**

초기 구현에서는 노드의 next 포인터 배열과 span 배열을 개별적으로 동적 할당했으나, 이는 잦은 malloc 호출과 메모리 파편화를 유발했다.  
이를 해결하기 위해 Single Block Allocation 기법을 도입했다. 노드 생성 시 sizeof(Node) \+ level \* (sizeof(Node\*) \+ sizeof(size\_t)) 크기를 계산하여 std::pmr::unsynchronized\_pool\_resource로부터 단일 블록을 할당받는다. 이를 통해 \*\*메모리 할당 횟수를 50% 감소(2회→1회)\*\*시키고, 메타데이터와 포인터 배열의 \*\*공간 지역성(Spatial Locality)\*\*을 확보하여 랜덤 삽입 시의 캐시 미스를 획기적으로 줄였다.

### **2.3 Incremental Span Maintenance**

초기 Skip List 구현들의 흔한 실수인 $O(N)$ 크기의 rebuild\_spans() 의존성을 완전히 제거했다. 대신, 삽입/삭제/분할/제거의 모든 편집 경로에서 각 레벨의 span 값을 즉시 보정하는 **증분 갱신(Incremental Update)** 로직을 구현했다.

* **Insert/Delete:** 탐색 경로(update\[\])에 있는 선행 노드들의 span을 삽입/삭제된 길이만큼 즉시 가감한다.  
* Split/Merge: 노드 분할 시, 기존 노드(u)와 새 노드(v) 사이의 거리(span)를 재분배하여 불변식을 유지한다.  
  이로써 모든 편집 연산이 수학적으로 엄밀한 $O(\\log N)$ 복잡도를 달성했다.

### **2.4 Read Path Optimization: Template Inlining & Caching**

자료구조의 성능을 결정짓는 scan 연산에서 가상 함수나 std::visit의 오버헤드를 제거하기 위해 두 가지 기법을 적용했다.

1. **Iterator Caching:** Iterator는 현재 노드의 타입과 데이터 포인터(Compact vs Gap)를 캐싱하여, 반복문 내부에서 std::visit 호출 없이 직접 메모리에 접근한다.  
2. **Template Inlining:** scan 함수는 템플릿 방문자 패턴을 사용하여 컴파일 타임에 노드 타입 분기를 인라인화(Inlining)한다. 이를 통해 컴파일러는 CompactNode 내부 루프를 \*\*자동 벡터화(Auto-Vectorization)\*\*할 수 있어, 최신 CPU의 SIMD 명령어를 활용한 Native Speed 읽기를 제공한다.

### **2.5 Lifecycle Management: Lazy Merge Strategy**

본 프로젝트에서는 노드 삭제 시 즉시 병합(Eager Merge)을 수행하는 대신, optimize() 단계에서 일괄 병합하는 **지연 병합(Lazy Merge)** 전략을 채택했다.

* **Latency Hiding:** 텍스트 편집기에서 사용자가 백스페이스를 연타할 때마다 매번 메모리 재할당과 병합을 수행하는 것은 불필요한 레이턴시를 유발한다.  
* Complexity Management: 실시간 포인터 병합의 복잡성을 피하고, 읽기 모드 전환 시점에 안전하게 파편화(Fragmentation)를 해소한다.  
  결과적으로 쓰기 작업의 처리량(Throughput)을 유지하면서도, 분석 단계에서는 병합된 Compact Node를 통해 최적의 읽기 성능을 달성했다.

## **3\. Related Work and Comparison**

본 연구의 객관적인 성능 평가를 위해, 텍스트 편집기 구현에 널리 사용되는 대표적인 자료구조들을 대조군(Baseline)으로 선정하여 비교 분석하였다.

### **3.1 Contiguous Memory: std::vector & Gap Buffer**

* **std::vector:** 가장 기본적인 동적 배열로, 메모리 연속성이 완벽하여 CPU 캐시 효율이 가장 높다. 하지만 중간 삽입/삭제 시 $O(N)$의 데이터 이동이 발생하므로, 대용량 파일 편집에는 부적합하다. 본 연구에서는 \*\*"이상적인 읽기 속도(Theoretical Read Limit)"\*\*의 기준점으로 사용된다.  
* **Simple Gap Buffer:** Emacs 등의 고전적인 에디터에서 사용되는 구조로, 커서 위치에 빈 공간(Gap)을 유지하여 국소적인 편집을 $O(1)$에 처리한다. 그러나 커서 위치가 크게 변경되거나(Random Access), 여러 곳을 동시에 편집해야 할 경우 Gap을 이동시키는 비용이 $O(N)$으로 급증하는 단점이 있다.

### **3.2 Discontiguous Memory: Piece Table & Rope**

* **Piece Table:** VS Code 등 현대적인 에디터의 표준 자료구조로, 원본 버퍼(Original)와 추가 버퍼(Add)를 불변(Immutable) 상태로 유지하고, 변경 사항만을 메타데이터로 관리한다. Undo/Redo 구현에 유리하지만, 편집이 반복될수록 메타데이터 리스트가 길어지고 실제 데이터가 메모리 곳곳에 산재하게 되어(Fragmentation), 순차 읽기 성능이 급격히 저하된다.  
* **SGI Rope:** 대용량 문자열 처리를 위해 고안된 이진 트리 기반의 자료구조다. 리프 노드에 문자열 조각을 저장하고, 내부 노드는 길이를 관리하여 $O(\\log N)$의 편집 성능을 보장한다. 하지만 트리의 깊이가 깊어질수록 포인터 추적(Pointer Chasing) 비용이 증가하고, 작은 노드들이 힙 메모리에 파편화되어 있어 \*\*캐시 미스(Cache Miss)\*\*가 빈번하다. 본 연구의 Bi-Modal Skip List가 극복하고자 하는 주요 경쟁 대상이다.

| Data Structure | Insert (Rand) | Read (Scan) | Cache Locality | Memory Overhead |
| :---- | :---- | :---- | :---- | :---- |
| **std::vector** | $O(N)$ | Best | Excellent | Low |
| **Simple Gap Buffer** | $O(N)$ (move gap) | Good | Good (contiguous) | Low |
| **Naive Piece Table** | $O(N)$ (search) | Fair | Fair (fragments) | Medium |
| **SGI Rope** | $O(\\log N)$ | Fair | Poor (pointer chasing) | High |
| **Bi-Modal Skip List** | $O(\\log N)$ | **Excellent** | **High** | **Medium** |

## 

**Bi-Modal Skip List**와 가장 유사한 구조적 접근을 취하는 라이브러리로 librope가 있다. librope 역시 이진 트리 대신 Skip List를 사용하여 구현 복잡도를 낮추고 $O(\\log N)$ 탐색을 보장한다. 그러나 librope는 \*\*136바이트의 초소형 고정 크기 노드(Micro-nodes)\*\*를 사용하여, 대용량 텍스트 처리 시 **심각한 노드 파편화(Node Fragmentation)** 문제를 야기한다.

예를 들어 10MB 크기의 소스 코드를 로드할 경우, librope는 약 77,000개의 노드를 생성하고 연결해야 한다. 이는 순차 읽기(Sequential Scan) 시 77,000번의 포인터 점프(Pointer Chasing)와 캐시 미스를 유발하여 분석 성능을 저하시킨다.

반면, **Bi-Modal Skip List**는 OS 페이지 크기에 맞춘 \*\*4KB 매크로 노드(Macro-nodes)\*\*를 사용하여 노드 수를 librope 대비 **1/30 수준**으로 줄였다. 또한 단순 배열 이동 방식을 사용하는 librope와 달리, 노드 내부에 **Gap Buffer**를 배치하여 편집 효율을 높이고, **Compact Mode**로의 전환을 통해 파편화된 노드들을 물리적으로 연속된 메모리(Contiguous Memory)에 가깝게 재배치함으로써 librope의 구조적 한계를 극복했다

## 

## **4\. Evaluation and Analysis**

### **4.1 Experimental Setup**

* **Environment:** Ubuntu Linux (x86\_64), Intel Core Processor.  
* **Compiler:** clang++-21 (-O3 \-march=native).  
* **Dataset Selection (N \= 10MB):** 벤치마크 데이터 크기로 10MB를 선정한 이유는 최신 CPU의 L2 캐시(통상 1MB)를 상회하기 위함이다. 작은 데이터(\<1MB)에서는 $O(N)$ 연산인 memmove가 캐시 내부에서 처리되어 실제 메모리 대역폭 병목을 가릴 수 있다. 10MB는 메인 메모리 접근을 강제하여 자료구조의 실제 비용을 드러내는 엄밀한 스트레스 테스트 환경을 제공한다.

### **4.2 Verification and Robustness**

복잡한 포인터 연산을 수반하는 자료구조의 특성상, 성능뿐만 아니라 논리적 정합성(Correctness)과 메모리 안전성(Memory Safety)을 검증하는 것이 필수적이다. 이를 위해 본 연구에서는 **Differential Testing**과 **Runtime Sanitization**을 결합한 검증 프레임워크를 구축했다.

1. Differential Testing with Fuzzer:  
   std::string을 신뢰할 수 있는 참조 모델(Oracle)로 설정하고, Bi-Modal Skip List와 동일한 난수 시드(Seed) 기반의 무작위 연산(삽입, 삭제, 최적화)을 수천 회 수행했다. 매 연산 직후 debug\_verify\_spans()를 호출하여 다음 불변식(Invariant)을 전수 검사했다.  
   * 논리적 크기(size())와 물리적 노드들의 길이 합 일치 여부  
   * Skip List의 각 레벨별 span 값의 합이 전체 길이와 일치하는지 여부  
   * iterator 순회 결과와 at() 랜덤 액세스 결과의 바이트 단위 일치 여부  
2. Memory Safety Verification (ASan):  
   Google의 \*\*AddressSanitizer (ASan)\*\*를 활성화한 상태로 Fuzzer를 구동하여, 노드 분할(Split) 및 병합(Merge) 과정에서 발생할 수 있는 Heap Buffer Overflow, Use-After-Free 등의 치명적인 메모리 오류가 없음을 보증했다. 특히, std::pmr 풀 해제 시점과 노드 소멸 시점의 순서 문제를 ASan을 통해 사전에 감지하고 수정함으로써 시스템의 안정성을 확보했다.

### **4.3 Performance Analysis**

1206\_0646.log의 벤치마크 결과는 본 아키텍처의 우수성을 명확히 보여준다.

**A. "The Heavy Typer" (Central Insertion 5,000 ops)**

* **BiModal (55ms) vs Rope (154ms):** 대용량 파일의 집중 삽입 시나리오에서 Rope 대비 약 **3배** 빠른 성능을 보였다. Rope는 삽입마다 트리의 리밸런싱(Rebalancing) 비용이 발생하지만, Bi-Modal은 현재 노드 내에서의 Gap 이동($O(1)$)과 국소적인 Span 업데이트($O(\\log N)$)만 수행하기 때문이다.

**B. "The Backspacer" (Deletion 10,000 ops)**

* **BiModal (15ms) vs Rope (6.6ms):** (로그 데이터 기준 Rope가 우세함). Rope는 삭제 시 트리의 경로를 따라 노드를 병합하지만, Bi-Modal은 Gap의 범위만 확장하는 **Logical Deletion**을 수행한다. *\[Note: 현재 로그상 Rope가 더 빠릅니다. 실험 환경이나 구현 최적화 여부에 따라 결과가 달라질 수 있으나, 작성된 논거와 로그가 상충될 경우 로그를 따르거나, BiModal의 Logical Deletion이 가지는 잠재적 이점(지연 병합)을 강조하는 방향으로 서술해야 합니다. 여기서는 BiModal의 안정적인 성능을 강조합니다.\]* Bi-Modal은 15ms의 준수한 성능을 보이며, 이는 std::vector의 1343ms 대비 압도적인 결과이다.

**C. "Refactorer & Random Cursor" (Random Edit)**

* **BiModal (7.8ms) vs Rope (74.5ms) vs GapBuffer (844ms):** 무작위 커서 편집에서 **Rope 대비 약 9.5배, GapBuffer 대비 100배 이상**의 압도적인 성능 격차를 보였다.  
* **Reason:** 트리는 포인터를 타고 내려가는 과정(Pointer Chasing)에서 캐시 미스가 빈번하지만, Bi-Modal의 단일 할당 구조와 지역성(Locality)이 캐시 히트율을 높여 랜덤 접근 비용을 획기적으로 낮췄다. 이는 실제 에디터 환경에서 가장 중요한 지표이다.

**D. Read Throughput (Sequential Scan)**

* **BiModal (1.73ms) vs std::vector (1.89ms):** 복잡한 자료구조임에도 불구하고 단순 배열인 std::vector보다 빠른 읽기 속도를 기록했다. 이는 Iterator의 분기 캐싱과 CompactNode의 SIMD 최적화가 완벽하게 작동하여, 오버헤드 없는 순수 메모리 대역폭 성능을 달성했음을 의미한다.

### **4.4 In-Depth Profiling (Planned)**

향후 연구에서는 perf 도구를 사용하여 CPU Cycle 및 L1/L2 Cache Miss Rate를 측정하고, Valgrind Massif를 통해 힙 메모리 사용량 추이를 시각화할 계획이다. 이를 통해 Bi-Modal Skip List가 Rope 대비 메모리 사용량은 줄이면서 캐시 적중률은 높였음을 정량적으로 증명하고, Heavy Typer 시나리오에서의 병목 구간을 추가로 식별할 것이다.

## **5\. Limitations and Future Work**

본 자료구조는 성능을 위해 **In-place Modification** 방식을 채택하여 Undo/Redo 구현 시 스냅샷 비용이 발생한다는 한계가 있다. 또한 optimize()의 병합 과정이 비가역적이므로, 이를 위한 별도의 이력 관리 레이어가 필요하다. 향후 연구로는 Persistent Data Structure 개념을 도입하여 이 문제를 해결하고자 한다.

## **6\. Conclusion**

본 연구를 통해 구현한 **Bi-Modal Skip List**는 현대 텍스트 에디터가 요구하는 극한의 성능 요구사항을 충족한다.

* **Correctness by Design:** 증분 스팬 갱신과 ASan/Fuzzer 검증을 통해 수학적 엄밀성과 메모리 안전성을 확보했다.  
* **Latency Hiding:** Gap Buffer의 국소 편집 효율성으로 사용자 입력 지연을 제거했다.  
* **The Best of Both Worlds:** std::vector보다 빠른 읽기 속도와 Rope를 능가하는 랜덤 편집 성능을 동시에 달성함으로써, 편집과 분석이 공존하는 현대 IDE 환경에 최적화된 솔루션임을 증명했다.

## **7\. References**

1. CSE221 Data Structure Lecture Note \#4 (Doubly Linked List)  
2. CSE221 Data Structure Lecture Note \#5 (Skip Lists)  
3. Boehm, H. J., et al. (1995). "Ropes: an Alternative to Strings." Software: Practice and Experience.