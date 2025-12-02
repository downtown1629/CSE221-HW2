# Bi-Modal Skip List: 워크로드 적응형 텍스트 자료구조

## 1. 개요 (Overview)

### 1.1 문제 정의 및 동기

현대 텍스트 처리 시스템(IDE, 코드 에디터, 실시간 협업 도구)은 명확히 구분되는 두 가지 워크로드 패턴을 보입니다.

**편집 단계 (Write-Heavy Phase)**

  - 사용자가 코드를 타이핑하는 단계
  - 커서 위치에서의 집중적인 삽입/삭제 발생 (높은 참조 지역성)
  - 국소적인 수정이 중요, 전체 순회는 드묾

**분석 단계 (Read-Heavy Phase)**

  - 컴파일러, 린터, LSP 서버가 전체 코드를 스캔
  - 순차적 접근 및 랜덤 액세스 모두 중요
  - 캐시 효율이 성능에 직접적 영향

### 1.2 기존 자료구조의 한계

| 자료구조 | 편집 성능 | 읽기 성능 | 주요 문제점 |
|---------|----------|----------|------------|
| `std::vector` | O(N) 삽입 | O(1) 읽기 | 중간 삽입 시 대량 데이터 이동 |
| Gap Buffer | O(1) 평균 | O(N) 랜덤 액세스 | 전체 순회 및 검색 비효율 |
| Skip List | O(log N) | O(log N) | 메모리 파편화로 캐시 미스 다발 |

### 1.3 제안: Bi-Modal Skip List

**핵심 아이디어**: 시스템 워크로드에 따라 노드의 내부 메모리 레이아웃을 **동적으로 변환**하는 하이브리드 자료구조

  - **Write Mode**: 노드를 Gap Buffer로 유지 → O(1) 편집 속도
  - **Read Mode**: 노드를 Compact Array로 압축 → 메모리 효율 및 캐시 히트율 극대화

## 2. 핵심 아키텍처 (Core Architecture)

### 2.1 노드의 이원적 상태 (Bi-Modal Nodes)

각 노드는 `std::variant`를 사용하여 두 가지 상태 중 하나를 가집니다.

#### A. Gap Node (Write-Optimized)

커서 주변에 gap을 배치하여 데이터 복사 없이 O(1) 삽입/삭제를 지원합니다. 편집이 빈번한 영역에 적합합니다.

#### B. Compact Node (Read-Optimized)

데이터가 물리적으로 연속 배치되어 SIMD 연산 및 CPU prefetch에 최적화된 상태입니다. Gap으로 인한 메모리 낭비가 없습니다.

### 2.2 상태 전환 메커니즘

```cpp
using NodeData = std::variant<GapNode, CompactNode>;

// Compact → Gap (편집 시작 시)
// 읽기 전용 노드에 수정 요청이 오면 즉시 Gap Node로 변환하여 여유 공간 확보
GapNode expand(const CompactNode& c);

// Gap → Compact (분석 모드 전환 시)
// 불필요한 Gap을 제거하고 메모리를 압축하여 연속성 확보
CompactNode compact(const GapNode& g);
```

## 3. Skip List 통합 구조

### 3.1 메모리 최적화 노드 구조 (Single Allocation)

초기 설계와 달리, 성능을 위해 `std::vector` 대신 **단일 메모리 블록 할당** 방식을 채택했습니다. 이는 `Node` 생성 시 `next` 포인터 배열과 `span` 배열을 하나의 메모리 덩어리로 할당하여 캐시 지역성(Cache Locality)을 극대화합니다.

```cpp
struct Node {
    NodeData data;        // GapNode or CompactNode
    Node** next;          // 레벨별 다음 노드 포인터 배열
    size_t* span;         // 각 레벨의 건너뛰기 거리 배열
    int level;            // 현재 노드의 높이

private:
    char* memory_block;   // 실제 할당된 단일 메모리 블록

public:
    Node(int lvl) : level(lvl) {
        // next 배열과 span 배열을 한 번의 new[]로 할당 (메모리 파편화 방지)
        size_t alloc_size = (sizeof(Node*) + sizeof(size_t)) * lvl;
        memory_block = new char[alloc_size];
        
        // 포인터 연결 (Placement)
        next = reinterpret_cast<Node**>(memory_block);
        span = reinterpret_cast<size_t*>(memory_block + (sizeof(Node*) * lvl));
    }
    // ...
};
```

### 3.2 전체 자료구조

```cpp
class BiModalText {
public:
    void insert(size_t pos, std::string_view s);
    void erase(size_t pos, size_t len); // 구현 완료
    char at(size_t pos) const;
    size_t size() const { return total_size; }
    
    // 워크로드 전환
    void optimize();  // Write Mode -> Read Mode
    
    // 고성능 순회
    template <typename Func> void scan(Func func) const;
    
private:
    static constexpr int MAX_LEVEL = 16;
    static constexpr size_t NODE_MAX_SIZE = 4096; // 4KB (OS 페이지 크기 고려)
    // ...
};
```

## 4. 핵심 연산 구현

### 4.1 위치 탐색 (Find Node)

Skip List의 특성을 이용하여 `O(log N)` 시간에 목표 노드를 찾습니다. 각 링크의 `span` 값을 누적하여 현재 논리적 위치를 계산합니다.

### 4.2 삽입 (Insert)

1.  **탐색**: 삽입 위치가 포함된 노드를 찾습니다.
2.  **모드 전환**: 노드가 `CompactNode`라면 `GapNode`로 확장(`expand`)합니다.
3.  **삽입**: `GapNode` 내부 버퍼에 데이터를 씁니다 (`O(1)`).
4.  **분할(Split)**: 노드 크기가 `NODE_MAX_SIZE`(4096)를 초과하면 두 개의 노드로 분할합니다.
      - *구현 상세*: 예외 안전성(Exception Safety)을 위해 새 노드를 먼저 생성하고 성공 시 기존 노드를 수정합니다.

### 4.3 최적화 및 병합 (Optimize)

`optimize()` 호출 시 전체 자료구조를 읽기 모드로 전환하고 파편화를 정리합니다.

```cpp
void BiModalText::optimize() {
    Node* curr = head->next[0];
    
    // Phase 1: Transmutation (Gap -> Compact)
    while (curr) {
        if (std::holds_alternative<GapNode>(curr->data)) {
            curr->data = compact(std::get<GapNode>(curr->data));
        }
        curr = curr->next[0];
    }
    
    // Phase 2: Defragmentation (작은 노드 병합)
    curr = head->next[0];
    while (curr && curr->next[0]) {
        Node* next_node = curr->next[0];
        size_t combined = curr->content_size() + next_node->content_size();
        
        // [중요 제약 조건]
        // 1. 합친 크기가 제한 이내여야 함
        // 2. 다음 노드가 Level 1 (바닥 노드)이어야 함
        // -> 상위 레벨 노드를 병합하면 이전 노드들의 포인터를 전부 수정해야 하므로 비용 과다
        if (combined <= NODE_MAX_SIZE && next_node->level == 1) {
            merge_nodes_internal(curr, next_node); 
            // 병합 후 curr는 이동하지 않음 (연쇄 병합 가능성)
        } else {
            curr = next_node;
        }
    }
}
```

### 4.4 데이터 순회 (Iteration & Scanning)

단순 `at()` 호출은 `O(log N)`이므로 전체 순회 시 `O(N log N)`이 되어 비효율적입니다. 이를 해결하기 위해 두 가지 고속 접근 방식을 구현했습니다.

1.  **Smart Iterator**: 현재 노드가 `CompactNode`인 경우, 가상 함수 호출 없이 포인터 연산만으로 버퍼를 순회하는 Fast Path를 제공합니다.
2.  **Scan 메서드 (`scan<Func>`)**:
    람다 함수를 인자로 받아 내부 루프를 수행합니다. 컴파일러가 루프를 인라인화(Inlining)하고 벡터화(SIMD)할 수 있어 가장 빠른 읽기 성능을 제공합니다.

<!-- end list -->

```cpp
// 사용 예시
text.scan([&](char c) {
    checksum += c; 
});
```

### 4.5 삭제 (Erase) - [구현 완료]

삭제 연산은 다음과 같이 처리됩니다.

1.  삭제 범위에 걸친 노드들을 순차적으로 방문합니다.
2.  각 노드 내에서 삭제할 길이만큼 Gap을 확장하여 논리적으로 데이터를 지웁니다.
3.  데이터가 모두 지워져 크기가 0이 된 노드는 리스트에서 물리적으로 제거(`remove_node`)합니다.

## 5. 성능 튜닝 파라미터

구현된 코드의 튜닝 값은 다음과 같습니다.

```cpp
// 노드 크기 제한 (OS Page Size와 유사하게 설정하여 캐시 효율 증대)
constexpr size_t NODE_MAX_SIZE = 4096; 

// 병합 최소 기준 (이보다 작으면 적극적으로 병합 시도)
constexpr size_t NODE_MIN_SIZE = 256;   

// 초기 Gap 크기
constexpr size_t DEFAULT_GAP_SIZE = 128;

// Skip List 최대 레벨
constexpr int MAX_LEVEL = 16;
```


## 6. 결론

구현된 **Bi-Modal Skip List**는 초기 설계의 개념을 유지하면서도, 실제 시스템(C++)에서의 성능을 고려하여 **메모리 레이아웃 최적화(Single Allocation)**와 **현실적인 병합 전략(Level 1 Merge)**을 적용했습니다. 그 결과 편집 시에는 Gap Buffer의 유연함을, 조회 시에는 연속된 메모리의 이점을 모두 취할 수 있었습니다.