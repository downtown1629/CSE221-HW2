
# Bi-Modal Skip List  
**상태 전환 기반 텍스트용 자료구조 설계 & 구현 로드맵**

---

## 1. 문제 동기 및 목표

현대 텍스트 편집/개발 환경(IDE, 실시간 협업 에디터 등)에는 대략 두 가지 뚜렷한 워크로드 모드가 있습니다.

1. **작성 모드 (Write-heavy Phase)**  
   - 사용자가 코드를 타이핑하거나 문서를 편집  
   - 특정 “커서 근처”에 삽입/삭제가 매우 빈번  
   - 랜덤 읽기보다 “국소적인 수정”이 중요

2. **읽기/분석 모드 (Read-heavy Phase)**  
   - 컴파일, 정적 분석, 전체 검색, 하이라이팅, LSP 분석 등  
   - 전체 텍스트를 순회하거나 큰 구간을 반복해서 읽음  
   - 이때는 캐시 친화적인 순차 접근이 중요

기존 자료구조들은 보통 이 둘 중 하나에만 특화되어 있습니다.

- **Gap Buffer**: 삽입/삭제는 빠르지만 랜덤 액세스/검색이 O(n)  
- **Skip List / Rope / Unrolled List**: 랜덤 액세스/검색은 빠르지만 편집 성능은 상대적으로 별로  
- **Gap-Node Skip List (JumpRope 류)**: 각 노드가 Gap Buffer라 편집은 강하지만, 읽기 모드에서도 gap이 남아 있어 메모리 낭비/캐시 비효율이 존재

**Bi-Modal Skip List**의 목표는 다음과 같습니다.

> “작성 모드에서는 Gap Buffer의 강점을, 읽기 모드에서는 연속된 배열 기반 구조의 캐시 효율을 얻자.”

이를 위해 **각 노드가 두 가지 메모리 레이아웃(Gap Node / Compact Node)을 오가며, 워크로드 모드에 따라 상태를 전환하는 Skip List**를 설계합니다.

---

## 2. 관련 구조 간단 요약

### 2.1 Standard Skip List

- 정렬된 키 시퀀스를 여러 레벨의 연결리스트로 표현
- 랜덤 레벨을 사용한 확률적 균형
- 평균 O(log n) 탐색/삽입/삭제
- 단점:
  - 각 노드마다 개별 할당 → 메모리 파편화
  - 포인터 체이닝 → 캐시 미스 많음

### 2.2 Gap Buffer

- 배열 + “gap(빈 구간)” 두 포인터 `(gap_start, gap_end)`
- 커서 주변에 gap을 위치시키면 삽입/삭제가 O(1) (평균)
- 문서 전체를 순회하거나 중간으로 멀리 점프하는 작업은 O(n)

### 2.3 Gap-Node Skip List / JumpRope 스타일

- 각 노드가 여러 문자를 담는 **unrolled node**이면서, 내부 표현이 Gap Buffer
- Skip List + Gap Buffer의 장점 결합
- 하지만:
  - 읽기 위주 작업에서도 gap이 그대로 존재
  - 메모리 낭비 + 캐시 비효율 (불연속한 메모리 영역)

---

## 3. Bi-Modal Skip List 개념

### 3.1 핵심 아이디어

각 노드는 두 상태 중 하나입니다.

1. **Gap Node (Write Mode)**  
   - 내부 표현: Gap Buffer  
   - 멤버:
     - `std::vector<char> buf;`
     - `size_t gap_start;`
     - `size_t gap_end;`
   - 커서 근처 삽입/삭제가 빠름

2. **Compact Node (Read Mode)**  
   - 내부 표현: 연속된 배열  
   - 멤버:
     - `std::vector<char> buf;` (gap 없음, 실제 데이터만)
   - 전체 순회, 랜덤 읽기 시 캐시 효율 극대화

**Bi-modal**이라는 이름은, 시스템이 다음 두 모드를 오가며 동작하기 때문입니다.

- 작성 모드: 삽입/삭제가 자주 일어나는 노드들을 Gap Node로 유지
- 읽기/분석 모드: Gap Node들을 Compact Node로 변환(Compaction)하여, 불필요한 gap 제거 + 인접 노드 병합

### 3.2 장점 요약

- **작성 모드**  
  - 커서 주변의 노드는 Gap Node → 국소 편집 빠름
  - Skip List 구조 덕에 문서 크기가 커져도 전체 복잡도는 관리 가능

- **읽기 모드**  
  - gap 제거 & 노드 병합으로 데이터가 더욱 큰 “덩어리 배열”에 들어감
  - 전체 순회/검색 시 캐시 미스 감소

---

## 4. 외부 인터페이스 설계

텍스트 전용 컨테이너라고 생각하고, 외부에서는 다음과 같이 사용하게 합니다.

```cpp
class BiModalText {
public:
    using value_type = char;

    // --- 기본 연산 ---
    void insert(size_t pos, std::string_view s);
    char  at(size_t pos) const;
    size_t size() const;

    // --- 모드/상태 관련 ---
    void compact();           // 전체 리스트를 순회하며 Gap → Compact, 필요시 병합

    // --- 디버깅/테스트용 ---
    std::string to_string() const;

private:
    // 내부 구현은 뒤에서 설명
};
````

### 4.1 사용 예시 (개발자 관점)

```cpp
BiModalText text;
text.insert(0, "Hello");
text.insert(5, " World");  // 중간 삽입

std::cout << text.to_string() << "\n"; // "Hello World"

// 컴파일/분석 직전에
text.compact();

// 분석용으로 전체 순회
for (size_t i = 0; i < text.size(); ++i) {
    char c = text.at(i);
    // ...
}
```

---

## 5. 내부 자료구조 설계

### 5.1 전제

* 문자 단위는 **ASCII `char`** 기준 (UTF-8/유니코드는 옵션 확장)
* 각 노드는 여러 문자를 저장하는 **블록(unrolled node)**이며, 이 블록의 내부 표현을 Gap/Compact로 바꿉니다.
* 전체는 **인덱스 기반 Skip List**로 관리:

  * “전역 pos를 주면, 어떤 노드와 그 노드 내부의 offset으로 매핑”하는 구조

---

### 5.2 Node 내부 표현

#### 5.2.1 GapNode

```cpp
struct GapNode {
    std::vector<char> buf;
    size_t gap_start;
    size_t gap_end;

    size_t size() const {
        return buf.size() - (gap_end - gap_start);
    }

    // 내부 인덱스 -> 실제 buf 인덱스 변환
    size_t physical_index(size_t logical_index) const {
        if (logical_index < gap_start) return logical_index;
        return logical_index + (gap_end - gap_start);
    }

    // gap을 target 위치로 이동
    void move_gap(size_t target);

    // gap 위치에서 삽입
    void insert_at(size_t pos, std::string_view s);

    // (옵션) 삭제
    void erase_at(size_t pos, size_t len);

    std::string to_string() const;
};
```

#### 5.2.2 CompactNode

```cpp
struct CompactNode {
    std::vector<char> buf;

    size_t size() const { return buf.size(); }

    char at(size_t i) const { return buf[i]; }

    std::string to_string() const {
        return std::string(buf.begin(), buf.end());
    }
};
```

#### 5.2.3 상태 전환: std::variant 사용

```cpp
using NodeData = std::variant<GapNode, CompactNode>;
```

```cpp
GapNode make_gap_from_compact(const CompactNode& c) {
    GapNode g;
    g.buf = c.buf;
    g.gap_start = g.buf.size();
    g.gap_end   = g.buf.size();
    return g;
}

CompactNode make_compact_from_gap(const GapNode& g) {
    CompactNode c;
    c.buf.reserve(g.size());
    // gap 앞 부분 복사
    c.buf.insert(c.buf.end(), g.buf.begin(), g.buf.begin() + g.gap_start);
    // gap 뒤 부분 복사
    c.buf.insert(c.buf.end(), g.buf.begin() + g.gap_end, g.buf.end());
    c.buf.shrink_to_fit();
    return c;
}
```

---

### 5.3 Skip List 노드 구조

각 노드는 variant 데이터 외에 skip list 연결을 위한 정보를 가집니다.

```cpp
struct Node {
    NodeData data;                  // GapNode 또는 CompactNode

    std::vector<Node*> next;        // 레벨별 next 포인터
    std::vector<size_t> span;       // 각 레벨에서 몇 개의 문자를 건너뛰는지

    size_t total_size_in_node() const {
        return std::visit([](auto const& n) {
            return n.size();
        }, data);
    }
};
```

* `next[level]` : 해당 레벨에서 오른쪽 노드
* `span[level]` : 해당 레벨을 따라 한 칸 이동할 때 건너뛰는 “문자 수”

### 5.4 Skip List 헤더 및 전체 구조

```cpp
class BiModalText {
public:
    using value_type = char;

    BiModalText();
    ~BiModalText();

    void insert(size_t pos, std::string_view s);
    char  at(size_t pos) const;
    size_t size() const;
    void compact();
    std::string to_string() const;

private:
    static constexpr int MAX_LEVEL = 16;   // 적당한 값
    Node* head;                            // dummy 헤더 노드
    int   current_level;
    size_t total_size;                     // 전체 문자 수

    int random_level() const;

    // pos 기준으로 삽입 위치를 찾는 헬퍼
    Node* find_node(size_t pos,
                    size_t& node_offset,
                    std::array<Node*, MAX_LEVEL>& update,
                    std::array<size_t, MAX_LEVEL>& rank) const;
};
```

* `find_node`는 skip list를 타고 내려가면서:

  * 각 레벨에서 어디까지 pos를 소모했는지(ranks),
  * 해당 pos 앞에 있는 노드들을 `update[]`에 저장하여 나중 삽입/연결에 사용.

---

## 6. 핵심 연산 설계

### 6.1 인덱스 탐색: find_node

**목표:**
“전역 pos를 주면 (노드, 해당 노드 내 offset, 업데이트용 선행 노드들)을 반환”

대략적인 알고리즘 스케치:

```cpp
Node* BiModalText::find_node(
    size_t pos,
    size_t& node_offset,
    std::array<Node*, MAX_LEVEL>& update,
    std::array<size_t, MAX_LEVEL>& rank) const
{
    Node* x = head;
    size_t accumulated = 0;

    for (int lvl = current_level - 1; lvl >= 0; --lvl) {
        while (x->next[lvl] &&
               accumulated + x->span[lvl] <= pos) {
            accumulated += x->span[lvl];
            x = x->next[lvl];
        }
        update[lvl] = x;
        rank[lvl] = accumulated;
    }

    Node* target = x->next[0]; // 실제 노드
    node_offset = pos - rank[0];
    return target;
}
```

* pos가 노드 길이를 넘어가는 경우는 boundary check 필요
* 단순화를 위해 pos가 항상 `[0, size()]` 범위라고 가정

---

### 6.2 삽입: insert(pos, s)

삽입 알고리즘 요약:

1. `find_node(pos, node_offset, update, rank)` 호출
2. 반환된 `target` 노드가:

   * `nullptr`인 경우 → 새 노드를 만들어 끝에 붙인다.
   * 있는 경우:

     * 노드가 `CompactNode`면 `GapNode`로 변환
     * GapNode 내부에서 `node_offset` 위치로 gap을 이동 → `s` 삽입
3. 노드가 너무 커지면 split:

   * 예: `NODE_MAX_SIZE`를 1024로 정하고, 그 이상이면 두 개 노드로 나누기
4. 새로운 노드가 생기면 skip list 레벨/포인터/스팬 수정

스텝별 좀 더 구체적 스케치:

```cpp
void BiModalText::insert(size_t pos, std::string_view s) {
    // 1. 위치 탐색
    std::array<Node*, MAX_LEVEL> update;
    std::array<size_t, MAX_LEVEL> rank;
    size_t node_offset = 0;
    Node* node = find_node(pos, node_offset, update, rank);

    total_size += s.size();

    // 2. 노드가 없는 경우 (빈 리스트 or 맨 끝 삽입)
    if (!node) {
        Node* new_node = new Node();
        // new_node를 GapNode로 초기화하고 s 전체 삽입
        // skip list 연결 업데이트
        return;
    }

    // 3. NodeData를 GapNode로 변환 (필요 시)
    if (std::holds_alternative<CompactNode>(node->data)) {
        CompactNode c = std::get<CompactNode>(node->data);
        node->data = make_gap_from_compact(c);
    }

    auto& g = std::get<GapNode>(node->data);
    g.move_gap(node_offset);
    g.insert_at(node_offset, s);

    // 4. 노드가 너무 커지면 split
    if (g.size() > NODE_MAX_SIZE) {
        // split_node(node);
    }

    // 5. span 정보 업데이트
    //    (노드 길이 변화에 따라 update[]를 기준으로 재계산)
}
```

---

### 6.3 읽기: at(pos)

1. `find_node(pos, node_offset, update, rank)` 호출
2. 해당 노드 내부 offset에서 문자 읽기

```cpp
char BiModalText::at(size_t pos) const {
    std::array<Node*, MAX_LEVEL> update;
    std::array<size_t, MAX_LEVEL> rank;
    size_t node_offset = 0;
    Node* node = find_node(pos, node_offset, update, rank);
    if (!node) {
        throw std::out_of_range("pos out of range");
    }

    return std::visit([&](auto const& n) {
        if constexpr (std::is_same_v<std::decay_t<decltype(n)>, GapNode>) {
            size_t idx = n.physical_index(node_offset);
            return n.buf[idx];
        } else {
            return n.buf[node_offset];
        }
    }, node->data);
}
```

---

### 6.4 compaction: compact()

**목표:**

* 모든 GapNode를 CompactNode로 변환
* (옵션) 너무 작은 노드들을 병합해 포인터 수를 줄임

단순 버전 알고리즘:

```cpp
void BiModalText::compact() {
    Node* x = head->next[0];
    while (x) {
        if (std::holds_alternative<GapNode>(x->data)) {
            GapNode g = std::get<GapNode>(x->data);
            x->data = make_compact_from_gap(g);
        }
        x = x->next[0];
    }

    // (옵션) 병합 로직
    // e.g. 인접 두 CompactNode의 합이 NODE_MIN_SIZE 이하이면 병합
}
```

이 함수는 **읽기/분석 모드로 전환되기 전에 명시적으로 호출**하는 것으로 가정합니다.
실제 시스템에서는 idle 시간에 백그라운드로 돌릴 수 있지만, 과제 스코프에서는 이렇게 명시적 호출만으로도 충분합니다.

---

## 7. 최소 구현 스펙 체크리스트

### 7.1 필수 기능 (MVP)

* [ ] **GapNode**

  * [ ] gap buffer 구조 및 삽입
  * [ ] `move_gap`, `insert_at`, `to_string`
* [ ] **CompactNode**

  * [ ] 단순 vector 기반 읽기, `to_string`
* [ ] **Gap ↔ Compact 전환**

  * [ ] `make_gap_from_compact`
  * [ ] `make_compact_from_gap`
* [ ] **Node + Skip List 골격**

  * [ ] `Node` 구조체 (`NodeData`, `next`, `span`)
  * [ ] `BiModalText` 클래스 기본 틀
  * [ ] 헤더 노드, `MAX_LEVEL`, `random_level`
* [ ] **핵심 연산**

  * [ ] `find_node(pos, ...)`
  * [ ] `insert(pos, s)`
  * [ ] `at(pos)`
  * [ ] `size()`
  * [ ] `to_string()`
* [ ] **compaction**

  * [ ] 모든 GapNode를 CompactNode로 바꾸는 `compact()`

### 7.2 옵션 기능 (있으면 좋은 것)

* [ ] 삭제 연산 `erase(pos, len)`
* [ ] 자동 compaction 정책 (N번 삽입 후 자동 호출 등)
* [ ] 노드 병합/분할 로직 정교화 (NODE_MAX_SIZE, NODE_MIN_SIZE)
* [ ] UTF-8 / 유니코드 지원
* [ ] 벤치마크 및 성능 측정 코드
* [ ] “항상 Compact인 Skip List” 또는 “항상 Gap인 Skip List”를 baseline으로 구현

---

## 8. 구현 로드맵 (단계별)

### Phase 0: 프로젝트 뼈대 & 테스트 환경

**목표:** 기본 C++ 프로젝트 골격과 간단한 테스트를 빠르게 준비.

* [ ] CMake/Makefile 설정
* [ ] `BiModalText`를 테스트할 메인 함수 또는 간단 테스트 프레임워크 준비
* [ ] `assert` 기반 단위 테스트용 함수 몇 개 만들어두기

---

### Phase 1: GapNode / CompactNode 단독 구현

**목표:** Skip List 없이, “노드 하나만 사용하는 mini 에디터”를 완성

* [ ] `GapNode` 구현

  * [ ] 생성자 & 기본 초기화
  * [ ] `move_gap`
  * [ ] `insert_at`
  * [ ] (옵션) `erase_at`
  * [ ] `to_string`
* [ ] `CompactNode` 구현

  * [ ] 기본 vector 래핑, `size`, `to_string`
* [ ] `Gap <-> Compact` 변환 함수
* [ ] 단위 테스트:

  * [ ] 특정 문자열로 GapNode 생성 → 여러 삽입/삭제 → `to_string` 확인
  * [ ] GapNode → CompactNode 변환 후 문자열 일치 확인

---

### Phase 2: 단일 레벨 연결 리스트 버전 (Skip List 없이)

**목표:** Bi-Modal 개념을 **연결리스트 수준에서 먼저 검증**

* [ ] `Node`에 `next`만 두고, `span`/레벨은 나중에
* [ ] `BiModalText` 초기 버전

  * [ ] `insert(pos, s)`:

    * [ ] 앞에서부터 순회하며 pos 위치 노드 찾기
    * [ ] Gap/Compact 변환 + 삽입
    * [ ] 노드가 너무 커지면 간단 split
  * [ ] `at(pos)`
  * [ ] `size`, `to_string`
  * [ ] `compact()`
* [ ] 간단 테스트:

  * [ ] 작은 문자열에 여러 번 중간 삽입 후 `to_string` 확인
  * [ ] `compact()` 호출 후에도 내용 동일한지 확인

> 이 단계만으로도 Bi-Modal 아이디어의 핵심(노드 상태 전환 + compaction)은 동작합니다. 마감이 급하면 여기까지만 해도 “동작하는 자료구조”로 보여줄 수 있습니다.

---

### Phase 3: Skip List로 업그레이드

**목표:** 연결리스트를 인덱스 기반 Skip List로 확장하여 성능 향상 구조 구현

* [ ] `Node`에 `next[level]`, `span[level]` 벡터 추가
* [ ] 헤더 노드에 `MAX_LEVEL`, `current_level` 도입
* [ ] `random_level()` 함수 구현
* [ ] `find_node(pos, ...)`를 skip list 기반으로 작성
* [ ] `insert(pos, s)`를 skip list 탐색 결과를 사용하도록 변경
* [ ] 삽입/분할 시 `span` 값 업데이트 로직 구현
* [ ] `at(pos)`도 skip list 기반 탐색 사용

---

### Phase 4: Bi-Modal 기능 다듬기 & 정책 추가

**목표:** 연구/보고서에 쓸 만한 정책/튜닝 요소 추가

* [ ] 노드 분할/split 기준 튜닝 (`NODE_MAX_SIZE`)
* [ ] (옵션) 노드 병합/merge 기준 (`NODE_MIN_SIZE`)
* [ ] (옵션) 자동 compaction 정책:

  * [ ] 누적 삽입 횟수 기반
  * [ ] GapNode 개수 기반
* [ ] 코드 정리 & 주석 정리 (보고서용 스니펫 추출 쉬우려면 중요)

---

### Phase 5: 비교용 구조 & 벤치마크

**목표:** 평가(Evaluation) 섹션에 사용할 실험 결과 생성

* [ ] 비교 대상 구조

  * [ ] `std::vector<char>` (baseline)
  * [ ] “항상 CompactNode만 사용하는 Skip List” or “항상 GapNode만 사용하는 Skip List”
  * [ ] Bi-Modal Skip List
* [ ] 시나리오별 측정

  * [ ] Typing Phase: 중간 인덱스에 N번 삽입
  * [ ] Compiling Phase: 전체 순회(모든 문자 합산) 시간 측정
* [ ] 결과를 CSV로 출력하고, 그래프/표는 Python/Excel로 그리기

---

## 9. 평가(벤치마크) 설계 간단 요약

보고서에서 주장할 수 있는 포인트:

1. **Typing Phase**

   * 입력: 초기 텍스트 길이 L, 중간 위치에 M번 삽입
   * 측정: 총 삽입 시간
   * 기대:

     * `std::vector`는 중간 삽입 O(L)씩 들어서 느림
     * Bi-Modal Skip List / Gap-Node Skip List는 전체적으로 더 빠름

2. **Compiling Phase**

   * 입력: 동일한 텍스트
   * 연산: 전체를 앞에서 끝까지 한 번 순회
   * 기대:

     * Bi-Modal은 compaction 후 큰 CompactNode 위주로 구성 → 캐시 친화 → Gap-Node Skip List보다 빠를 가능성

실제 수치가 완벽하게 “논문급”일 필요는 없습니다.
**상대적인 경향성**만 보여도 과제 평가에서는 충분한 설득력을 가질 수 있습니다.

---

## 10. 확장 아이디어 (선택)

시간과 여유가 있다면 다음 확장을 고려할 수 있습니다.

* **삭제 연산 완전 지원 (erase, replace 등)**
* **부분 문자열 추출 (substr)**, 검색(`find`) 등 텍스트 편집기 인터페이스 확장
* **UTF-8 지원**: `char` 대신 코드 포인트 단위 관리
* **멀티 스레드 환경에서의 동시성 지원** (고난이도)

---

## 마무리

이 문서의 목표는:

1. Bi-Modal Skip List의 **아이디어를 처음 보는 사람도 이해**할 수 있도록 개념을 설명하고,
2. 실제로 C++17/20 기반으로 **구현을 시작할 수 있는 정도로 구체적인 설계와 로드맵**을 제공하는 것입니다.

위의 순서대로 구현을 진행하면:

* Phase 1~2에서 **노드 상태 전환 + Bi-Modal의 핵심 개념을 먼저 검증**하고
* Phase 3~5에서 Skip List/벤치마크까지 확장하여
* “현대적 텍스트 워크로드에 특화된 새로운 자료구조”라는 스토리를 과제에서 충분히 어필할 수 있습니다.

