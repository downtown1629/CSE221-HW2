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
| Gap-Node Skip List | 빠름 | 중간 | gap이 항상 존재해 메모리 낭비 |

### 1.3 제안: Bi-Modal Skip List

**핵심 아이디어**: 시스템 워크로드에 따라 노드의 내부 메모리 레이아웃을 **동적으로 변환**하는 하이브리드 자료구조

- **Write Mode**: 노드를 Gap Buffer로 유지 → O(1) 편집 속도
- **Read Mode**: 노드를 Compact Array로 압축 → 메모리 효율 및 캐시 히트율 극대화

## 2. 핵심 아키텍처 (Core Architecture)

### 2.1 노드의 이원적 상태 (Bi-Modal Nodes)

각 노드는 `std::variant`를 사용하여 두 가지 상태 중 하나를 가집니다.

#### A. Gap Node (Write-Optimized)

```cpp
struct GapNode {
    std::vector<char> buf;
    size_t gap_start;
    size_t gap_end;
    
    size_t size() const {
        return buf.size() - (gap_end - gap_start);
    }
    
    size_t physical_index(size_t logical_idx) const {
        return (logical_idx < gap_start) 
            ? logical_idx 
            : logical_idx + (gap_end - gap_start);
    }
    
    void move_gap(size_t target);
    void insert_at(size_t pos, std::string_view s);
    void erase_at(size_t pos, size_t len);
};
```

**특징**:
- 커서 주변에 gap을 배치하여 데이터 복사 없이 O(1) 삽입/삭제
- 편집이 빈번한 영역에 적합

#### B. Compact Node (Read-Optimized)

```cpp
struct CompactNode {
    std::vector<char> buf;  // gap 없음, 순수 데이터
    
    size_t size() const { return buf.size(); }
    char at(size_t i) const { return buf[i]; }
};
```

**특징**:
- 데이터가 물리적으로 연속 배치
- SIMD 연산, CPU prefetch에 최적
- 전체 순회 및 검색 시 캐시 효율 극대화

### 2.2 상태 전환 메커니즘

```cpp
using NodeData = std::variant<GapNode, CompactNode>;

// Compact → Gap (편집 시작)
GapNode expand(const CompactNode& c) {
    GapNode g;
    g.buf = c.buf;
    g.buf.reserve(c.buf.size() + GAP_SIZE);  // 추가 공간 확보
    g.gap_start = g.gap_end = c.buf.size();
    return g;
}

// Gap → Compact (분석 직전)
CompactNode compact(const GapNode& g) {
    CompactNode c;
    c.buf.reserve(g.size());
    c.buf.insert(c.buf.end(), g.buf.begin(), g.buf.begin() + g.gap_start);
    c.buf.insert(c.buf.end(), g.buf.begin() + g.gap_end, g.buf.end());
    c.buf.shrink_to_fit();
    return c;
}
```

**Lifecycle**:
1. **Expansion**: 읽기 전용 노드에 수정 요청 시 즉시 Gap Node로 변환
2. **Compaction**: `optimize()` 호출 시 모든 Gap Node를 Compact Node로 변환
3. **Merge**: 인접한 작은 Compact Node들을 병합하여 노드 수 감소

## 3. Skip List 통합 구조

### 3.1 인덱스 기반 Skip List

각 노드는 **문자 개수**를 기준으로 Skip List를 구성합니다.

```cpp
struct Node {
    NodeData data;                    // GapNode or CompactNode
    std::vector<Node*> next;          // 레벨별 다음 노드
    std::vector<size_t> span;         // 각 레벨에서 건너뛰는 문자 수
    
    size_t content_size() const {
        return std::visit([](auto const& n) { 
            return n.size(); 
        }, data);
    }
};
```

**span의 의미**: `span[level]`은 해당 레벨에서 한 칸 이동할 때 건너뛰는 문자의 개수

### 3.2 전체 자료구조

```cpp
class BiModalText {
public:
    void insert(size_t pos, std::string_view s);
    char at(size_t pos) const;
    size_t size() const { return total_size; }
    void optimize();  // Read Mode 전환
    
private:
    static constexpr int MAX_LEVEL = 16;
    static constexpr size_t NODE_MAX_SIZE = 1024;
    static constexpr size_t NODE_MIN_SIZE = 256;
    static constexpr size_t GAP_SIZE = 128;
    
    Node* head;  // dummy header
    int current_level;
    size_t total_size;
    
    Node* find_node(size_t pos, size_t& offset,
                    std::array<Node*, MAX_LEVEL>& update,
                    std::array<size_t, MAX_LEVEL>& rank) const;
    int random_level() const;
};
```

## 4. 핵심 연산 구현

### 4.1 위치 탐색: find_node

```cpp
Node* BiModalText::find_node(
    size_t pos,
    size_t& node_offset,
    std::array<Node*, MAX_LEVEL>& update,
    std::array<size_t, MAX_LEVEL>& rank) const 
{
    Node* x = head;
    size_t accumulated = 0;
    
    // 상위 레벨부터 하향 탐색
    for (int lvl = current_level - 1; lvl >= 0; --lvl) {
        while (x->next[lvl] && 
               accumulated + x->span[lvl] <= pos) {
            accumulated += x->span[lvl];
            x = x->next[lvl];
        }
        update[lvl] = x;
        rank[lvl] = accumulated;
    }
    
    Node* target = x->next[0];
    node_offset = pos - rank[0];
    return target;
}
```

### 4.2 삽입: insert(pos, s)

```cpp
void BiModalText::insert(size_t pos, std::string_view s) {
    std::array<Node*, MAX_LEVEL> update;
    std::array<size_t, MAX_LEVEL> rank;
    size_t offset = 0;
    
    Node* node = find_node(pos, offset, update, rank);
    
    if (!node) {
        // 새 노드 생성
        node = create_new_node(s);
        link_node(node, update, rank);
    } else {
        // Compact → Gap 변환 (필요 시)
        if (std::holds_alternative<CompactNode>(node->data)) {
            node->data = expand(std::get<CompactNode>(node->data));
        }
        
        // Gap Node에 삽입
        auto& gap = std::get<GapNode>(node->data);
        gap.move_gap(offset);
        gap.insert_at(offset, s);
        
        // 노드 크기 제한 초과 시 분할
        if (gap.size() > NODE_MAX_SIZE) {
            split_node(node, update, rank);
        }
    }
    
    // span 업데이트
    update_spans(update, rank, s.size());
    total_size += s.size();
}
```

### 4.3 최적화: optimize()

```cpp
void BiModalText::optimize() {
    Node* x = head->next[0];
    
    // Phase 1: Gap → Compact 변환
    while (x) {
        if (std::holds_alternative<GapNode>(x->data)) {
            x->data = compact(std::get<GapNode>(x->data));
        }
        x = x->next[0];
    }
    
    // Phase 2: 작은 노드 병합
    x = head->next[0];
    while (x && x->next[0]) {
        Node* next = x->next[0];
        if (x->content_size() + next->content_size() < NODE_MAX_SIZE) {
            merge_nodes(x, next);
        } else {
            x = next;
        }
    }
}
```

## 5. 개발 로드맵

### Phase 1: 노드 기본 구조 (1-2일)
- [ ] `GapNode` 구조체 구현
  - [ ] gap 이동, 삽입, 삭제 로직
- [ ] `CompactNode` 구조체 구현
- [ ] 상태 전환 함수 (`expand`, `compact`)
- [ ] **검증**: 단일 노드에서 여러 삽입 후 올바른 문자열 출력

### Phase 2: 단순 연결 리스트 (2-3일)
- [ ] `Node` 구조체 (skip list 포인터 없이)
- [ ] `BiModalText` 기본 틀
  - [ ] 선형 탐색 기반 `insert`, `at`, `size`
  - [ ] 기본 `optimize()` 구현
- [ ] **검증**: 중간 삽입 100회 후 `to_string()` 정확성 확인

### Phase 3: Skip List 통합 (3-4일)
- [ ] `next`, `span` 벡터 추가
- [ ] `find_node` Skip List 탐색 구현
- [ ] `random_level()` 구현
- [ ] 삽입 시 span 업데이트 로직
- [ ] **검증**: 10,000자 텍스트에서 랜덤 삽입 성능 측정

### Phase 4: 최적화 및 병합 로직 (2-3일)
- [ ] 노드 분할 (`split_node`)
- [ ] 노드 병합 (`merge_nodes`)
- [ ] span 재계산 최적화
- [ ] **검증**: compaction 전후 데이터 무결성 확인

### Phase 5: 벤치마크 및 평가 (2-3일)
- [ ] 비교 대상 구현 (vector, list, 단일 모드 skip list)
- [ ] 시나리오 A/B 측정
- [ ] 결과 그래프 생성

## 6. 평가 계획 (Evaluation)

### 시나리오 A: "The Typer" (편집 성능)

**설정**:
- 초기 텍스트: 10MB (10,000,000자)
- 연산: 중간 위치(5,000,000)에 10,000자 연속 삽입
- 측정: 총 삽입 시간

**예상 결과**:
```
std::vector:        ~5000ms  (매 삽입마다 5MB 이동)
std::list:          ~50ms    (하지만 메모리 비효율)
Bi-Modal (Write):   ~30ms    (Gap Buffer 효과)
```

### 시나리오 B: "The Compiler" (조회 성능)

**설정**:
- 동일 텍스트
- 연산: `optimize()` 후 전체 순회 (모든 문자 합산)
- 측정: 순회 시간

**예상 결과**:
```
std::vector:        ~10ms    (연속 메모리)
std::list:          ~150ms   (캐시 미스 다발)
Bi-Modal (Read):    ~15ms    (compaction 후 캐시 효율)
```

### 추가 측정 지표
- 메모리 사용량 (peak memory)
- 캐시 미스율 (perf stat 사용 시)
- compaction 오버헤드

## 7. 구현 시 고려사항

### 7.1 C++ 버전 선택
**권장: C++17**
- `std::variant`: 노드 상태 표현
- Structured bindings: 코드 간결성
- `if constexpr`: 컴파일 타임 분기

**선택적 C++20/23 기능**:
- `std::span`: 부분 문자열 참조
- Ranges: `optimize()` 구현 간결화
- `std::unreachable()`: 최적화 힌트

### 7.2 성능 튜닝 파라미터

```cpp
// 노드 크기 제한
constexpr size_t NODE_MAX_SIZE = 1024;  // 분할 기준
constexpr size_t NODE_MIN_SIZE = 256;   // 병합 기준

// Gap 크기
constexpr size_t GAP_SIZE = 128;        // expansion 시 여유 공간

// Skip List 레벨
constexpr int MAX_LEVEL = 16;           // log2(MAX_CHARS)
constexpr double P = 0.25;              // 레벨 증가 확률
```

### 7.3 메모리 관리 전략

**노드 풀 사용 고려**:
```cpp
class NodePool {
    std::vector<std::unique_ptr<Node>> pool;
    std::vector<Node*> free_list;
public:
    Node* allocate();
    void deallocate(Node* n);
};
```

이는 malloc/free 오버헤드를 줄이고 메모리 지역성을 개선합니다.

## 8. 확장 가능성

### 8.1 필수 확장
- **삭제 연산**: `erase(pos, len)`
- **부분 문자열**: `substr(pos, len)`
- **검색**: `find(pattern)`

### 8.2 고급 확장
- **UTF-8 지원**: 코드 포인트 단위 인덱싱
- **Rope 통합**: 노드 자체를 Rope로 구성
- **Concurrent 버전**: RCU 기반 동시성 제어
- **Persistent 구조**: 버전 관리 지원

## 9. 파일 구조

```
STUDENT_ID_BIMODAL/
├── include/
│   ├── BiModalText.hpp      # 메인 인터페이스
│   ├── GapNode.hpp          # Gap Buffer 구현
│   ├── CompactNode.hpp      # Compact Array 구현
│   └── SkipList.hpp         # Skip List 기반 구조
├── src/
│   ├── BiModalText.cpp      # 구현
│   └── Utils.cpp            # 헬퍼 함수
├── tests/
│   ├── test_correctness.cpp # 정합성 테스트
│   └── test_benchmark.cpp   # 성능 벤치마크
├── scripts/
│   ├── run_bench.sh         # 자동 실행
│   └── plot_results.py      # 결과 시각화
├── CMakeLists.txt
├── README.md
└── report.pdf
```

## 10. 결론

**Bi-Modal Skip List**는 다음과 같은 점에서 차별화됩니다:

1. **워크로드 인식**: 시스템 상태에 따라 메모리 레이아웃을 동적 조정
2. **실용성**: 실제 IDE/에디터의 사용 패턴에 최적화
3. **균형**: 편집과 읽기 성능 사이의 trade-off를 동적으로 관리

이는 단순히 기존 자료구조를 결합한 것이 아니라, **"언제 어떤 표현을 사용할지"**에 대한 전략적 접근입니다. 실제 소프트웨어 엔지니어링(Text Editors, Database Logs, Version Control)에서 마주하는 문제에 대한 실용적 해법을 제시합니다.