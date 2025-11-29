
-----

# Bi-Modal Skip List: 동적 상태 전환을 통한 편집 및 조회 성능 최적화

## 1. 개요 (Overview)

### 1.1 배경 및 문제 정의

현대적인 텍스트 처리 시스템(IDE, 코드 에디터, 실시간 협업 도구)의 워크로드는 명확하게 구분되는 두 가지 단계를 가집니다.

1.  **편집 단계 (Edit Phase):** 사용자가 코드를 타이핑하는 단계. 커서 위치에서의 집중적인 삽입/삭제가 발생합니다. (참조 지역성 높음)
2.  **분석 단계 (Read/Analysis Phase):** 컴파일러나 린터(Linter)가 전체 코드를 스캔하거나, 특정 심볼을 검색하는 단계. (순차적 접근 및 랜덤 액세스 중요)

기존 자료구조들은 이 두 가지를 동시에 만족시키지 못합니다.

  * `std::vector`: 읽기는 빠르지만, 중간 삽입 시 $O(N)$의 데이터 이동이 발생합니다.
  * `Gap Buffer`: 편집은 빠르지만, 랜덤 액세스와 대규모 검색이 느립니다.
  * `Standard Skip List`: 삽입/검색은 $O(\log N)$으로 준수하지만, 잦은 노드 할당으로 인한 메모리 파편화(Fragmentation)로 **캐시 효율(Cache Locality)**이 매우 떨어집니다.

### 1.2 제안: Bi-Modal Skip List

**Bi-Modal Skip List**는 시스템의 상태에 따라 노드의 내부 구조를 **동적으로 변환(Transmute)**하는 하이브리드 자료구조입니다.

  * **Write Mode:** 노드를 **Gap Buffer**로 유지하여 $O(1)$ 편집 속도를 보장합니다.
  * **Read Mode:** 노드를 **Compact Array**로 압축하여 메모리 사용량을 줄이고 CPU 캐시 히트율을 극대화합니다.

-----

## 2. 핵심 아키텍처 (Core Architecture)

### 2.1 노드 상태의 이원화 (Bi-Modal Nodes)

각 노드는 `std::variant`를 사용하여 런타임에 두 가지 상태 중 하나를 가집니다.

#### **A. Gap Node (Write-Optimized)**

  * **구조:** 데이터 배열 + `gap_start`, `gap_end` 인덱스.
  * **특징:** 텍스트 에디터의 Gap Buffer와 동일합니다. 커서 이동과 삽입이 데이터 복사 없이 이루어집니다.
  * **사용 시점:** 사용자가 타이핑 중이거나 수정이 빈번한 영역.

#### **B. Compact Node (Read-Optimized)**

  * **구조:** 빈 공간(Gap)이 제거된 순수한 `std::vector` 형태.
  * **특징:** 데이터가 물리적으로 연속되어 있어 SIMD 연산이나 CPU 프리패치(Prefetch)에 유리합니다. 내부에서 이진 탐색(Binary Search)이 가능합니다.
  * **사용 시점:** 편집이 멈춘 유휴 상태(Idle)이거나, 컴파일/저장 작업 수행 시.

### 2.2 상태 전환 메커니즘 (Lifecycle)

이 자료구조의 **Novelty(차별점)**는 바로 이 전환 과정에 있습니다.

1.  **Expansion (Compact $\rightarrow$ Gap):**
      * 읽기 전용 노드에 수정 요청이 들어오면, 즉시 Gap을 포함한 넉넉한 크기의 `Gap Node`로 변환됩니다.
2.  **Compaction (Gap $\rightarrow$ Compact):**
      * `optimize()` 함수가 호출되면(또는 타이머 트리거), 리스트를 순회하며 `Gap Node`들의 빈 공간을 제거합니다.
      * **Merge Optimization:** 인접한 두 `Compact Node`의 크기가 작다면 하나로 병합하여 스킵 리스트의 전체 노드 개수를 줄입니다.

-----

## 3. 구현 상세 (Implementation Details)

**제약 사항:** C++17 이상 사용, 외부 라이브러리 금지.
### 3.1 데이터 구조 정의 (Modern C++)

```cpp
#include <vector>
#include <variant>
#include <memory>
#include <iostream>

// 상태 1: 쓰기 최적화 노드
struct GapBlock {
    std::vector<char> buffer;
    size_t gap_start;
    size_t gap_end;
    // ... Gap Buffer 로직 ...
};

// 상태 2: 읽기 최적화 노드
struct CompactBlock {
    std::vector<char> data;
    // ... Binary Search 로직 ...
};

// Bi-Modal 노드
struct Node {
    // 핵심: 노드 컨텐츠의 다형성 (Polymorphism)
    std::variant<GapBlock, CompactBlock> content;
    
    // Skip List의 포워드 포인터
    std::vector<Node*> forward;
    
    Node(int level) : forward(level, nullptr), content(GapBlock{}) {}
};
```

### 3.2 C++23 기능 활용 전략 (선택 사항)

구현의 간결함과 최적화를 위해 최신 기능을 활용합니다.

  * **`Deducing this`:** `iterator` 구현 시 `const`와 `non-const` 버전의 중복 코드 제거.
  * **`std::unreachable()`:** `std::visit` 사용 시 컴파일러 최적화 힌트 제공.
  * **`std::vector::append_range`:** Compaction 과정에서 데이터 병합 시 가독성 향상.

-----

## 4. 개발 로드맵 (Development Roadmap)

이 순서대로 개발하면 과제 마감일까지 안정적으로 완성할 수 있습니다.

### **Phase 1: 기본 골격 (Skeleton)**

  * [ ] `Node` 구조체 정의 (아직 `variant` 없이 단순 `vector`로 시작).
  * [ ] 표준 Skip List 알고리즘 (Insert, Search) 구현.
  * [ ] **검증:** `int`형 키를 넣어 정렬이 유지되는지 확인.

### **Phase 2: Gap Buffer 구현 (Write Mode)**

  * [ ] `GapBlock` 구조체 구현 (Insert, Delete, MoveCursor).
  * [ ] Skip List의 노드를 `GapBlock`으로 교체.
  * [ ] **검증:** 단일 노드 내에서 커서 이동 및 글자 삽입이 올바르게 되는지 확인.

### **Phase 3: Bi-Modal 통합 (The Novelty)**

  * [ ] `std::variant<GapBlock, CompactBlock>` 적용.
  * [ ] `insert()` 함수 수정: 타겟 노드가 `CompactBlock`이면 `GapBlock`으로 변환(Expansion) 후 삽입.
  * [ ] `compact_all()` 함수 구현: 모든 노드를 순회하며 `GapBlock`을 `CompactBlock`으로 변환.

### **Phase 4: 성능 벤치마크 및 리포트 작성**

  * [ ] `main_bench.cpp` 작성 (시나리오 A/B 구현).
  * [ ] `std::vector` 및 `std::list`와 성능 비교 측정.
  * [ ] 결과 데이터를 바탕으로 `report.pdf` 그래프 작성.

-----

## 5. 파일 구조 제안 (Project Structure)



```text
STUDENT_ID_HW2/
├── src/
│   ├── BiModalSkipList.hpp    // 핵심 구현 (Header-only 추천)
│   ├── main_test.cpp          // 정합성 검증용 (Correctness)
│   ├── main_bench.cpp         // 성능 측정용 (Evaluation)
│   └── Makefile               // 빌드 스크립트
├── run_bench.sh               // 자동 실행 스크립트
── README.txt                 // 컴파일 및 실행 방법
└── report.pdf                 // 최종 보고서
```

-----

## 6. 평가 계획 (Evaluation Plan)

리포트의 **"3. Evaluation and Analysis"** 섹션을 위한 실험 시나리오입니다.

### 시나리오 A: "The Typer" (편집 성능)

  * **상황:** 10MB 텍스트 파일의 중간 지점에서 10,000 글자를 연속으로 타이핑.
  * **예상 결과:**
      * `std::vector`: 매 삽입마다 뒤쪽 데이터를 밀어내느라 매우 느림 ($O(N)$).
      * `Bi-Modal`: 현재 노드(Gap Buffer)에서만 연산하므로 `std::list`급으로 빠름 ($O(1)$).

### 시나리오 B: "The Compiler" (조회 성능)

  * **상황:** 편집이 끝난 후, `compact()`를 수행하고 처음부터 끝까지 데이터를 읽음(Iteration).
  * **예상 결과:**
      * `std::list` / `Standard Skip List`: 메모리가 여기저기 흩어져 있어(Cache Miss) 느림.
      * `Bi-Modal`: 데이터가 압축되어 있어 `std::vector`에 근접하는 빠른 스캔 속도를 보임.

-----

## 7. 결론 및 기대 효과 (Conclusion)

이 프로젝트는 단순히 기존 자료구조를 구현하는 것을 넘어, **"워크로드의 상태 변화"**에 주목하여 메모리 레이아웃을 최적화한다는 점에서 창의적입니다. 이는 실제 소프트웨어 엔지니어링(Text Editors, Database Log Managers)에서 마주하는 문제를 해결하는 실용적인 접근 방식입니다.