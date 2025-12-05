#pragma once

#include <array>
#include <random>
#include <cassert>
#include <memory_resource>
#include "Nodes.hpp"

// 스킵 리스트용 상수들
constexpr int MAX_LEVEL = 16;
constexpr double P = 0.25;


class BiModalText {
public:
    BiModalText() : head(nullptr), total_size(0) {
        head = create_node(MAX_LEVEL);
        std::random_device rd;
        gen = std::mt19937(rd());
        dist = std::uniform_real_distribution<>(0.0, 1.0);
    }

    ~BiModalText() {
        clear();
        if (head) {   // 안전 장치
            destroy_node(head);
            head = nullptr;
        }
    }

    // 복사/대입 금지 (Node 구조를 복사하려면 deep copy가 필요 → 비현실적)
    BiModalText(const BiModalText&) = delete;
    BiModalText& operator=(const BiModalText&) = delete;

    // 이동도 금지 (skiplist pointer graph 이동은 위험 부담 큼)
    BiModalText(BiModalText&&) noexcept = delete;
    BiModalText& operator=(BiModalText&&) noexcept = delete;

    #ifdef BIMODAL_DEBUG
    // span, total_size, at()/to_string() 일관성 검사
    bool debug_verify_spans(std::ostream& os = std::cerr) const;

    // 레벨 0 기준 노드 구조 덤프
    void debug_dump_structure(std::ostream& os = std::cerr) const;
    #endif

    // --- [Move Up] Iterator Definition & Smart Caching ---
    class Iterator {
        const Node* curr_node;
        size_t offset;
        
        // --- [캐싱 변수] ---
        enum class Mode { None, Compact, Gap };
        Mode mode;
        const char* compact_ptr;
        size_t cached_len;    // 현재 노드의 전체 길이
        const char* gap_front_ptr;
        size_t gap_front_len;
        const char* gap_back_ptr;
        size_t gap_back_len;

        void update_cache() {
            if (!curr_node) {
                mode = Mode::None;
                compact_ptr = nullptr;
                cached_len = 0;
                gap_front_ptr = gap_back_ptr = nullptr;
                gap_front_len = gap_back_len = 0;
                return;
            }
            
            // 노드 타입에 따라 길이와 포인터를 미리 가져옴 (std::visit 1회 수행)
            if (std::holds_alternative<CompactNode>(curr_node->data)) {
                const auto& c_node = std::get<CompactNode>(curr_node->data);
                mode = Mode::Compact;
                compact_ptr = c_node.buf.data();
                cached_len = c_node.buf.size();
                gap_front_ptr = gap_back_ptr = nullptr;
                gap_front_len = gap_back_len = 0;
            } else { // GapNode fast path
                const auto& g_node = std::get<GapNode>(curr_node->data);
                mode = Mode::Gap;
                cached_len = g_node.size();
                gap_front_ptr = g_node.buf.data();
                gap_front_len = g_node.gap_start;
                gap_back_ptr = g_node.buf.data() + g_node.gap_end;
                gap_back_len = g_node.buf.size() - g_node.gap_end;
                compact_ptr = nullptr;
            }
        }

    public:
        Iterator(const Node* node, size_t off) : curr_node(node), offset(off) {
            update_cache();
        }

        char operator*() const {
            if (!curr_node) return '\0';
            if (mode == Mode::Compact) {
                return compact_ptr[offset];
            } else if (mode == Mode::Gap) {
                if (offset < gap_front_len) {
                    return gap_front_ptr[offset];
                }
                size_t back_offset = offset - gap_front_len;
                return gap_back_ptr[back_offset];
            }
            return '\0';
        }

        Iterator& operator++() {
            if (!curr_node) return *this;
            
            offset++;
            
            // [최적화] std::visit 호출 없이 캐싱된 길이와 비교
            if (offset >= cached_len) {
                curr_node = curr_node->next[0];
                offset = 0;
                update_cache(); // 노드가 바뀔 때만 캐시 갱신 (비용 발생)
            }
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return curr_node != other.curr_node || offset != other.offset;
        }
    };
    
    Iterator begin() const { return Iterator(head->next[0], 0); }
    Iterator end() const { return Iterator(nullptr, 0); }
    
    // --- [Ultimate Read Optimization] Internal Iterator ---
    // 람다 함수(func)를 받아서 모든 문자에 대해 실행합니다.
    // 컴파일러가 내부 루프를 강력하게 인라인/벡터화할 수 있습니다.
    template <typename Func>
    void scan(Func func) const {
        Node* curr = head->next[0];
        while (curr) {
            assert(!curr->data.valueless_by_exception());
            // std::visit 오버헤드를 노드당 1회로 줄임
            std::visit([&](auto const& n) {
                using T = std::decay_t<decltype(n)>;
                
                if constexpr (std::is_same_v<T, CompactNode>) {
                    // [Fast Path] 연속된 메모리 -> 컴파일러가 SIMD 최적화하기 딱 좋음
                    const char* data = n.buf.data();
                    size_t sz = n.buf.size();
                    for (size_t i = 0; i < sz; ++i) {
                        func(data[i]);
                    }
                } else {
                    // GapNode: 두 덩어리로 나눠서 처리
                    const char* data = n.buf.data();
                    // Part 1: Gap 앞
                    for (size_t i = 0; i < n.gap_start; ++i) func(data[i]);
                    // Part 2: Gap 뒤
                    size_t sz = n.buf.size();
                    for (size_t i = n.gap_end; i < sz; ++i) func(data[i]);
                }
            }, curr->data);
            
            curr = curr->next[0];
        }
    }
    // --- Main Operations ---

    void insert(size_t pos, std::string_view s) {
        if (pos > total_size) throw std::out_of_range("Pos out of range");

        std::array<Node*, MAX_LEVEL> update;
        std::array<size_t, MAX_LEVEL> rank;
        size_t node_offset = 0;

        Node* target = find_node(pos, node_offset, update, rank);

        // [리팩토링] 빈 리스트 특수 케이스 -> Early Return 처리
        if (!target) {
            if (total_size == 0) {
                target = create_node(random_level());
                std::get<GapNode>(target->data).insert(0, s); // GapNode임을 확신하므로 바로 접근
                
                for (int i = 0; i < MAX_LEVEL; ++i) {
                    if (i < target->level) {
                        head->next[i] = target;
                    } else {
                        head->next[i] = nullptr;
                    }
                    head->span[i] = target->content_size();
                }
                total_size += s.size();
#ifdef BIMODAL_DEBUG
                debug_verify_spans();
#endif
                return; // 여기서 함수 종료!
            } else {
                 throw std::runtime_error("Unexpected null target on non-empty list");
            }
        } 
        
        // --- 이하 일반 케이스 (else 블록 제거로 들여쓰기 감소) ---

        if (std::holds_alternative<CompactNode>(target->data)) {
            target->data = expand(std::get<CompactNode>(target->data));
        }
        
        // ... (나머지 로직 그대로)
        std::get<GapNode>(target->data).insert(node_offset, s);

        for (int i = 0; i < MAX_LEVEL; ++i) {
            if (update[i]) {
                update[i]->span[i] += s.size();
            }
        }

        if (target->content_size() > NODE_MAX_SIZE) {
            split_node(target, update);
        }
        
        total_size += s.size(); // *주의: Early return 했으므로 여기 도달하는 건 일반 케이스뿐임
#ifdef BIMODAL_DEBUG
        debug_verify_spans();
#endif
    }

    char at(size_t pos) const {
    if (pos >= total_size) throw std::out_of_range("Index out of range");

    Node* x = head;
    size_t accumulated = 0;
    for (int i = MAX_LEVEL - 1; i >= 0; --i) {
        while (x->next[i] && (accumulated + x->span[i] <= pos)) {  // ✅ <= 통일
            accumulated += x->span[i];
            x = x->next[i];
        }
    }

    Node* target = x->next[0];
    if (!target) {
#ifdef BIMODAL_DEBUG
        debug_dump_structure(std::cerr);
#endif
        throw std::runtime_error("Node structure corruption");
    }

    size_t offset = pos - accumulated;
    // ✅ NEW: find_node 동일 skip 루프 (boundary off==sz → next off=0)
    while (target && offset >= target->content_size()) {
        accumulated += target->content_size();
        target = target->next[0];
        offset = pos - accumulated;
    }
    if (!target || offset >= target->content_size()) {
#ifdef BIMODAL_DEBUG
        std::cerr << "Final OOB: off=" << offset << " >= sz=" << target->content_size() << "\n";
        debug_dump_structure(std::cerr);
#endif
        throw std::runtime_error("Final offset OOB");
    }

    return std::visit([offset](auto const& n) { return n.at(offset); }, target->data);
}


    // Iterator를 타지 않고, 노드 내부 버퍼를 통째로 append 하여 대역폭 활용 극대화
    std::string to_string() const {
        std::string res;
        res.reserve(total_size);
        Node* curr = head->next[0]; // Level 0 순회
        while (curr) {
            assert(!curr->data.valueless_by_exception());
            std::visit([&](auto const& n) {
                // 노드 타입에 따라 최적화된 블록 복사 수행
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, CompactNode>) {
                    // CompactNode: 한 방에 복사
                    res.append(n.buf.data(), n.buf.size());
                } else {
                    // GapNode: Gap을 건너뛰고 두 덩어리로 복사
                    // Part 1: Gap 앞
                    res.append(n.buf.data(), n.gap_start);
                    // Part 2: Gap 뒤
                    size_t back_len = n.buf.size() - n.gap_end;
                    res.append(n.buf.data() + n.gap_end, back_len);
                }
            }, curr->data);
            curr = curr->next[0];
        }
        return res;
    }

    

    void optimize() {
        Node* curr = head->next[0];

        // [Phase 1] Transmutation: 모든 GapNode를 CompactNode로 변환
        // - 메모리 단편화를 줄이고 읽기 속도(SIMD 친화적)를 확보합니다.
        while (curr) {
            if (std::holds_alternative<GapNode>(curr->data)) {
                curr->data = compact(std::get<GapNode>(curr->data));
            }
            curr = curr->next[0];
        }

    }
    
    size_t size() const { return total_size; }

        
    void clear() {
        if (!head) return; 

        // [중요 수정] 루프 시작점은 head가 아니라 head->next[0]이어야 합니다.
        Node* curr = head->next[0]; 
        
        while (curr) {
            Node* next = curr->next[0];
            destroy_node(curr); // 데이터 노드만 삭제 (279번째 줄 추정)
            curr = next;
        }

        // head는 살아있으므로 재사용을 위해 초기화합니다.
        // 만약 위 루프에서 curr = head 로 시작했다면, 여기서 크래시가 납니다.
        for (int i = 0; i < MAX_LEVEL; ++i) {
            head->next[i] = nullptr; // (286번째 줄 추정)
            head->span[i] = 0; 
        }

        total_size = 0;
    }

    void erase(size_t pos, size_t len) {
        if (pos >= total_size) return;
        if (pos + len > total_size) len = total_size - pos;

        while (len > 0) {
            std::array<Node*, MAX_LEVEL> update;
            std::array<size_t, MAX_LEVEL> rank;
            size_t offset = 0;

            Node* target = find_node(pos, offset, update, rank);
            if (!target) break;

            size_t available = target->content_size() - offset;
            size_t del_len = std::min(len, available);

            if (std::holds_alternative<CompactNode>(target->data)) {
                target->data = expand(std::get<CompactNode>(target->data));
            }

            for (int i = 0; i < MAX_LEVEL; ++i) {
                if (update[i]) {
                    update[i]->span[i] -= del_len;
                }
            }

            std::get<GapNode>(target->data).erase(offset, del_len);
            
            total_size -= del_len;
            len -= del_len;

            if (target->content_size() == 0) {
                remove_node(target, update);
            }
        }
#ifdef BIMODAL_DEBUG
        debug_verify_spans();
#endif
    }

private:
    static constexpr int MAX_LEVEL = 16;
    static constexpr size_t NODE_MAX_SIZE = 4096; 
    
    std::pmr::unsynchronized_pool_resource pool;
    Node* head;
    size_t total_size;
    std::mt19937 gen;
    std::uniform_real_distribution<> dist;

    size_t node_allocation_size(int level) const {
        size_t next_bytes = sizeof(Node*) * level;
        size_t span_bytes = sizeof(size_t) * level;
        return sizeof(Node) + next_bytes + span_bytes;
    }

    Node* create_node(int level) {
        size_t total_bytes = node_allocation_size(level);
        void* raw = pool.allocate(total_bytes, alignof(Node));
        auto* node = new(raw) Node(level);
        char* aux = static_cast<char*>(raw) + sizeof(Node);
        node->initialize_links(aux);
        return node;
    }

    void destroy_node(Node* node) {
        if (!node) return;
        size_t total_bytes = node_allocation_size(node->level);
        node->~Node();
        pool.deallocate(node, total_bytes, alignof(Node));
    }

    void rebuild_spans() {
        if (!head) return;

        Node* pred = head;
        Node* node = head->next[0];
        while (node) {
            pred->span[0] = node->content_size();
            pred = node;
            node = node->next[0];
        }
        if (pred) pred->span[0] = 0;

        Node* base = head;
        while (base) {
            for (int lvl = 1; lvl < base->level; ++lvl) {
                Node* target = base->next[lvl];
                size_t distance = 0;

                if (!target) {
                    Node* walker = base->next[0];
                    while (walker) {
                        distance += walker->content_size();
                        walker = walker->next[0];
                    }
                    base->span[lvl] = distance;
                    continue;
                }

                Node* walker = base->next[0];
                while (walker && walker != target) {
                    distance += walker->content_size();
                    walker = walker->next[0];
                }

                if (!walker) {
                    // target vanished? fall back to tail distance
                    walker = base->next[0];
                    distance = 0;
                    while (walker) {
                        distance += walker->content_size();
                        walker = walker->next[0];
                    }
                    base->span[lvl] = distance;
                } else {
                    distance += walker->content_size();
                    base->span[lvl] = distance;
                }
            }
            base = base->next[0];
        }
    }

    int random_level() {
        int lvl = 1;
        while (dist(gen) < P && lvl < MAX_LEVEL) lvl++;
        return lvl;
    }

    Node* find_node(size_t pos, size_t& node_offset,
                    std::array<Node*, MAX_LEVEL>& update,
                    std::array<size_t, MAX_LEVEL>& rank) const 
    {
        Node* x = head;
        size_t accumulated = 0;
        
        for(int i=0; i<MAX_LEVEL; ++i) update[i] = head;

        for (int i = MAX_LEVEL - 1; i >= 0; --i) {
            while (x->next[i] && (accumulated + x->span[i] < pos)) {
                accumulated += x->span[i];
                x = x->next[i];
            }
            update[i] = x;
            rank[i] = accumulated;
        }

        Node* target = x->next[0];
        
        if (target) {
            node_offset = pos - accumulated;
            while (target && node_offset >= target->content_size()) {
                if (!target->next[0]) break; 
                
                accumulated += target->content_size();

                for (int lvl = 0; lvl < target->level; ++lvl) {
                    update[lvl] = target;
                    rank[lvl] = accumulated;
                }
                
                target = target->next[0];
                node_offset = pos - accumulated;
            }
        }
        return target;
    }
  
    
    void split_node(Node* u, std::array<Node*, MAX_LEVEL>& update) {
        auto& u_gap = std::get<GapNode>(u->data);
        
        // 1. 분할 크기 계산
        //
        //  - total_size: 현재 노드 u가 가지고 있는 전체 문자 수
        //  - split_point: 앞쪽에 남길 문자 수
        //  - v_size: 뒤쪽으로 분리해서 새 노드 v로 옮길 문자 수
        //
        size_t total_size = u_gap.size();
        size_t split_point = total_size / 2;
        size_t v_size = total_size - split_point;
        
        // 2. [중요] 새로운 노드 'v' 먼저 생성
        //
        //  - split 과정에서 예외가 발생할 수 있으므로,
        //    u의 구조를 건드리기 전에 v를 먼저 안전하게 생성한다.
        //  - new_level은 u의 level을 넘지 않도록 clamp 한다.
        //
        int u_levels = u->level;
        int new_level = random_level();
        if (new_level > u_levels) new_level = u_levels;
        
        Node* v = create_node(new_level);
        
        try {
            // 3. split_right()를 사용하여 u에서 v로 데이터 이동
            //
            //  - split_right(v_size)는 "오른쪽 v_size 만큼"을 잘라내서 반환한다.
            //  - 호출 후:
            //      * u_gap : [앞부분 split_point 문자]만 남도록 내부 버퍼가 변경됨
            //      * 반환값 : [뒷부분 v_size 문자]를 담은 GapNode
            //  - 그 반환값을 v_gap에 대입해서 새 노드 v의 데이터로 사용한다.
            //
            auto& v_gap = std::get<GapNode>(v->data);
            v_gap = u_gap.split_right(v_size);
            // 이 시점에서:
            //  - u_gap.size() == split_point
            //  - v_gap.size() == v_size
        } catch (...) {
            // v 생성 이후 split 과정에서 예외가 나면
            // v를 정리한 뒤 예외를 그대로 다시 던져서 상위에서 처리하게 한다.
            destroy_node(v);
            throw;
        }
        
        // --- 이 시점부터는 예외가 발생하지 않는다고 가정 (No-Throw Section) ---
        //     포인터/스팬 갱신 중에 예외가 터지면 skip list의 불변식이 깨질 수 있으므로,
        //     그 이전에 예외 가능성이 있는 작업(split_right, 할당 같은 것들)을 모두 끝낸다.
        
        // 4. 포인터 및 span 갱신 (Linkage & Span Update)
        //
        // [span의 의미 요약]
        //  - 이 구현에서 span[i] 는 "해당 레벨 i에서, 이 포인터를 한 번 따라갔을 때
        //    텍스트 상에서 건너뛰는 문자 수"를 뜻한다.
        //  - 즉, x->span[i] 는 "x에서 x->next[i] (그리고 그 사이에 묶여 있는 노드들)을
        //    통해 한 번에 점프하는 전체 길이"라고 볼 수 있다.
        //  - find_node 에서:
        //
        //      while (x->next[i] && (accumulated + x->span[i] < pos)) {
        //          accumulated += x->span[i];
        //          x = x->next[i];
        //      }
        //
        //    이런 식으로 사용되므로, 각 레벨에서 "점프 거리"만 맞게 유지하면
        //    전체 인덱스 계산이 일관된다.
        //
        // [불변식]
        //  - split 전/후에도 "어떤 노드에서 더 먼 노드까지의 총 점프 거리"는
        //    그대로 유지되어야 한다.
        //  - 아래 span 갱신은 이 불변식을 지키기 위한 것이다.
        //
        for (int i = 0; i < MAX_LEVEL; ++i) {
            // update[i]는 "레벨 i에서 u 바로 앞에 있는 노드"를 의미한다.
            // 일부 레벨에서는 u가 존재하지 않을 수 있으므로 필터링한다.
            if (!update[i] || update[i]->next[i] != u) continue;
            
            // [중요 포인트: update[i]->span[i] -= v_size 가 필요한 이유]
            //
            //  - split 전 (레벨 i):
            //      update[i] --(span = S)--> u --(span = U)--> ...
            //    라고 하면, update[i] 에서 그 다음-next... 를 따라가며
            //    도달하는 어떤 노드까지의 총 점프 거리는 S + U 라고 할 수 있다.
            //
            //  - split 후에는 u가 [앞부분], v가 [뒷부분]을 가지며,
            //    레벨 i 에서 v를 "사이에 끼우는" 경우:
            //
            //      update[i] --(S - v_size)--> u --(v_size)--> v --(U)--> ...
            //
            //    가 되어야 전체 거리가 (S - v_size) + v_size + U = S + U 로 보존된다.
            //
            //  - 만약 여기에서 v_size 를 빼지 않으면:
            //      update[i] --(S)--> u --(v_size)--> v --(U)--> ...
            //    합이 S + v_size + U 가 되어, split 전보다 v_size 만큼 더 길어져
            //    인덱스 계산이 틀어지게 된다.
            //
            //  - 따라서 "u에서 뒤쪽 v_size 만큼을 떼어냈다"는 사실을
            //    update[i] 쪽 span 에서도 반영해야 한다.
            //
            update[i]->span[i] -= v_size;
            
            if (i < new_level) {
                // [케이스 1] 이 레벨에서 v가 실제로 존재하는 경우
                //
                //   (split 전)
                //      update[i] --S--> u --U--> next
                //
                //   (split 후)
                //      update[i] --(S - v_size)--> u --(v_size)--> v --(U)--> next
                //
                //   - u->span[i] 에는 "u에서 v까지의 거리" = v_size 를 기록한다.
                //   - v->span[i] 에는 "v에서 그 다음 노드까지의 거리"로 기존 u->span[i]
                //     ( = U )를 그대로 넘겨준다.
                //
                v->next[i] = u->next[i];
                u->next[i] = v;
                v->span[i] = u->span[i];  // v에서 다음까지의 거리 = 기존 u의 거리(U)
                u->span[i] = v_size;      // u에서 v까지의 거리 = v의 문자 수
            } else {
                // [케이스 2] 이 레벨에서 v는 존재하지 않는 경우
                //
                //   (split 전)
                //      update[i] --S--> u --U--> next
                //
                //   (split 후, 레벨 i 기준에서는 v를 "건너뛰는" 구조)
                //      update[i] --(S - v_size)--> u --(U + v_size)--> next
                //
                //   - 여기서는 레벨 i에서 v를 따로 노출하지 않으므로
                //     u->span[i] 하나가 u + v 를 모두 커버해야 한다.
                //   - 그래서 u->span[i] 에 v_size 를 더해서 전체 거리를 맞춘다.
                //
                //   총합: (S - v_size) + (U + v_size) = S + U (불변식 유지)
                //
                u->span[i] += v_size;
            }
        }
    }

    
    void remove_node(Node* target, const std::array<Node*, MAX_LEVEL>& update) {
        if (!target) return;  // ✅ null 안전

        size_t removed_len = target->content_size();
        for (int i = 0; i < MAX_LEVEL; ++i) {
            Node* prev = update[i];
            if (!prev || !prev->next[i]) continue;
            if (i < target->level && prev->next[i] == target) {
#ifdef BIMODAL_DEBUG
                assert(prev->span[i] >= removed_len);
#endif
                prev->span[i] = prev->span[i] - removed_len + target->span[i];
                prev->next[i] = target->next[i];
            } else {
#ifdef BIMODAL_DEBUG
                assert(prev->span[i] >= removed_len);
#endif
                prev->span[i] -= removed_len;
            }
        }
        destroy_node(target);
    }


};

#ifdef BIMODAL_DEBUG
bool BiModalText::debug_verify_spans(std::ostream& os) const {
    bool ok = true;
    // 1) level 0에서 content_size 합 == total_size?
    size_t sum0 = 0;
    const Node* curr = head->next[0];
    while (curr) {
        sum0 += curr->content_size();
        curr = curr->next[0];
    }
    if (sum0 != total_size) {
        os << "[DEBUG FAIL] L0 sum0=" << sum0 << " != total=" << total_size << "\n";
        ok = false;
    }

    // 2) 각 레벨 span 합 == total_size?
    for (int lvl = 0; lvl < MAX_LEVEL; ++lvl) {
        if (!head->next[lvl]) continue;  // ✅ 고레벨 구조 없음 → skip
        size_t acc = 0;
        const Node* x = head;
        while (x) {
            acc += x->span[lvl];
            if (!x->next[lvl]) break;
            x = x->next[lvl];
        }
        if (acc != total_size) {
            os << "[DEBUG FAIL] L" << lvl << " span_sum=" << acc << " != total=" << total_size << "\n";
            ok = false;
        }
    }

    // 3) to_string() size 체크 + at() vs to_string() 샘플링
    std::string full_str = to_string();
    if (full_str.size() != total_size) {
        os << "[DEBUG FAIL] to_string.size()=" << full_str.size() << " != total=" << total_size << "\n";
        ok = false;
    }
    const size_t N_CHECK = std::min<size_t>(total_size, 5000);
    for (size_t p = 0; p < N_CHECK; ++p) {
        if (at(p) != full_str[p]) {
            os << "[DEBUG FAIL] at(" << p << ")='" << at(p) << "' != to_str='" << full_str[p] << "'\n";
            ok = false;
            break;
        }
    }

    // 4) 각 레벨 개별 span이 실제 거리와 일치하는지 검증
    const Node* base = head;
    while (base) {
        for (int lvl = 0; lvl < base->level; ++lvl) {
            const Node* target = base->next[lvl];
            size_t distance = 0;
            const Node* walker = base->next[0];

            if (!target) {
                while (walker) {
                    distance += walker->content_size();
                    walker = walker->next[0];
                }
            } else {
                while (walker && walker != target) {
                    distance += walker->content_size();
                    walker = walker->next[0];
                }
                if (!walker) {
                    os << "[DEBUG FAIL] span path missing at lvl=" << lvl << "\n";
                    ok = false;
                    break;
                }
                distance += walker->content_size();
            }

            if (distance != base->span[lvl]) {
                os << "[DEBUG FAIL] node span mismatch lvl=" << lvl
                   << " distance=" << distance
                   << " stored=" << base->span[lvl]
                   << "\n";
                ok = false;
            }
        }
        base = base->next[0];
    }
    return ok;
}

void BiModalText::debug_dump_structure(std::ostream& os) const {
    os << "=== BiModalText DUMP (total_size=" << total_size << ") ===\n";
    const Node* curr = head->next[0];
    size_t off = 0;
    int idx = 0;
    while (curr) {
        os << "N" << idx << "(off=" << off << ", sz=" << curr->content_size()
           << ", lvl=" << curr->level << ") ";

        if (std::holds_alternative<CompactNode>(curr->data)) {
            const CompactNode& cn = std::get<CompactNode>(curr->data);
            os << "[COMPACT buf=" << cn.buf.size() << "]";
        } else {
            const GapNode& gn = std::get<GapNode>(curr->data);
            os << "[GAP buf=" << gn.buf.size()
               << " gap=[" << gn.gap_start << "," << gn.gap_end << ")"
               << " logical=" << gn.size() << "]";
        }

        // span 샘플 (상위 3레벨만)
        for (int l = 0; l < std::min(3, curr->level); ++l) {
            os << " L" << l << ":" << curr->span[l];
        }
        os << "\n";

        off += curr->content_size();
        curr = curr->next[0];
        ++idx;
    }
    os << "=== END DUMP ===\n";
}
#endif  // BIMODAL_DEBUG
