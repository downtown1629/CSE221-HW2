#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <variant>
#include <algorithm>
#include <stdexcept>
#include <random>
#include <array>
#include <memory>

constexpr size_t DEFAULT_GAP_SIZE = 128;



// --- 1. Compact Node ---
struct CompactNode {
    std::vector<char> buf;
    
    // 생성자
    CompactNode() = default;
    CompactNode(std::vector<char>&& data) : buf(std::move(data)) {}

    size_t size() const { return buf.size(); }
    char at(size_t i) const { return buf[i]; }
    
    std::string to_string() const {
        return std::string(buf.begin(), buf.end());
    }
};

// --- 2. Gap Node ---
struct GapNode {
    // 런타임 상수 (필요에 따라 클래스 외부로 빼거나 조정 가능)
    static constexpr size_t DEFAULT_GAP_SIZE = 128;

    std::vector<char> buf;
    size_t gap_start;
    size_t gap_end;

    // 생성자: 기본 용량 할당
    GapNode(size_t capacity = DEFAULT_GAP_SIZE) {
        if (capacity < DEFAULT_GAP_SIZE) capacity = DEFAULT_GAP_SIZE;
        buf.resize(capacity);
        gap_start = 0;
        gap_end = capacity;
    }

    // 논리적 크기 (Gap 제외)
    size_t size() const {
        return buf.size() - (gap_end - gap_start);
    }

    // 논리 인덱스 -> 물리 인덱스 변환
    size_t physical_index(size_t logical_idx) const {
        if (logical_idx < gap_start) return logical_idx;
        return logical_idx + (gap_end - gap_start);
    }

    // 문자 접근
    char at(size_t i) const {
        return buf[physical_index(i)];
    }

    // Gap 이동 (커서 이동)
    void move_gap(size_t target_logical_idx) {
        if (target_logical_idx == gap_start) return;

        if (target_logical_idx > gap_start) {
            // Gap을 오른쪽으로 이동 (데이터를 왼쪽으로 복사)
            // [Data A][Gap][Data B] -> [Data A][Data B_part][Gap]
            size_t dist = target_logical_idx - gap_start;
            std::copy(buf.begin() + gap_end, 
                      buf.begin() + gap_end + dist, 
                      buf.begin() + gap_start);
            gap_start += dist;
            gap_end += dist;
        } else {
            // Gap을 왼쪽으로 이동 (데이터를 오른쪽으로 복사)
            // [Data A][Gap] -> [Data A_part][Gap][Data A_rest]
            size_t dist = gap_start - target_logical_idx;
            // 겹치는 영역 안전 처리를 위해 copy_backward 사용 권장
            std::copy_backward(buf.begin() + target_logical_idx, 
                               buf.begin() + gap_start, 
                               buf.begin() + gap_end);
            gap_start -= dist;
            gap_end -= dist;
        }
    }

    // 삽입 (Insert)
    void insert(size_t pos, std::string_view s) {
        move_gap(pos);
        
        // 공간 부족 시 확장
        if (gap_end - gap_start < s.size()) {
            expand_buffer(s.size());
        }

        // 데이터 복사 (반복문 대신 copy 사용)
        std::copy(s.begin(), s.end(), buf.begin() + gap_start);
        gap_start += s.size();
    }

    // 삭제 (Erase) - 핵심 기능 추가
    void erase(size_t pos, size_t len) {
        if (pos + len > size()) {
             // 안전장치: 범위를 벗어나면 가능한 만큼만 삭제하거나 예외 처리
             len = size() - pos; 
        }
        
        move_gap(pos); // 삭제할 위치 바로 앞으로 Gap 이동
        
        // Gap의 끝부분을 늘려서 데이터를 '삼킴' (논리적 삭제)
        // [A][Gap][B C D] -> delete 1 char (B) -> [A][Gap...][C D]
        gap_end += len; 
    }

    // 버퍼 확장
    void expand_buffer(size_t needed) {
        size_t old_cap = buf.size();
        // 최소 2배 혹은 필요한 만큼 + 여유분
        size_t new_cap = old_cap * 2 + needed + DEFAULT_GAP_SIZE;
        size_t back_part_size = old_cap - gap_end;
        
        std::vector<char> new_buf(new_cap);
        
        // Gap 앞부분 복사
        std::copy(buf.begin(), buf.begin() + gap_start, new_buf.begin());
        
        // Gap 뒷부분 복사 (새 버퍼의 끝쪽에 배치)
        std::copy(buf.begin() + gap_end, buf.end(), new_buf.end() - back_part_size);
        
        buf = std::move(new_buf);
        gap_end = new_cap - back_part_size; 
    }

    // [최적화] 노드 분할을 위한 Suffix 추출 (string 변환 제거)
    // 현재 노드에서 뒷부분(suffix_len 만큼)을 잘라내어 새 GapNode로 반환
    GapNode split_right(size_t suffix_len) {
        size_t total_len = size();
        size_t split_idx = total_len - suffix_len; // 분할 지점

        // 1. Gap을 분할 지점으로 이동
        move_gap(split_idx);
        
        // 이제 구조는: [Prefix Data] [GAP] [Suffix Data]
        // Suffix Data는 buf[gap_end] 부터 끝까지임.

        // 2. 새 노드 생성 및 데이터 이동
        GapNode new_node(suffix_len + DEFAULT_GAP_SIZE);
        
        // Suffix 데이터를 새 노드의 앞부분으로 복사
        auto suffix_start_it = buf.begin() + gap_end;
        std::copy(suffix_start_it, buf.end(), new_node.buf.begin());
        
        // 새 노드 설정: 데이터는 앞에 몰려있고, Gap은 그 뒤에 존재
        new_node.gap_start = suffix_len;
        new_node.gap_end = new_node.buf.size(); // Gap이 끝까지 차지

        // 3. 현재 노드(Prefix) 정리
        // 뒷부분 데이터를 삭제하는 것과 같음 -> Gap을 버퍼 끝까지 확장해버림
        // 물리적 메모리를 줄이진 않고(shrink_to_fit X), 논리적으로만 끊음
        gap_end = buf.size(); 

        return new_node;
    }

    // 디버깅용
    std::string to_string() const {
        std::string res;
        res.reserve(size());
        res.append(buf.begin(), buf.begin() + gap_start);
        res.append(buf.begin() + gap_end, buf.end());
        return res;
    }
};

using NodeData = std::variant<GapNode, CompactNode>;


GapNode expand(const CompactNode& c) {
    // 기존 데이터 크기 + 여유 공간(Gap) 만큼 할당
    GapNode g(c.buf.size() + GapNode::DEFAULT_GAP_SIZE);
    
    // 데이터 복사: CompactNode의 모든 데이터를 GapNode의 앞부분으로 복사
    std::copy(c.buf.begin(), c.buf.end(), g.buf.begin());
    
    // Gap 설정: 데이터 바로 뒤부터 끝까지를 Gap으로 설정
    // [Data A B C][GAP . . .]
    g.gap_start = c.buf.size();
    g.gap_end = g.buf.size();
    
    return g;
}

// 2. Compact: GapNode -> CompactNode (읽기 모드 전환)
// 메모리 사용량을 줄이고 캐시 효율을 높이기 위해 Gap을 제거합니다.
CompactNode compact(const GapNode& g) {
    CompactNode c;
    c.buf.reserve(g.size()); // 정확한 데이터 크기만큼만 예약

    // Gap 앞부분 복사
    c.buf.insert(c.buf.end(), g.buf.begin(), g.buf.begin() + g.gap_start);
    
    // Gap 뒷부분 복사
    c.buf.insert(c.buf.end(), g.buf.begin() + g.gap_end, g.buf.end());
    
    // (선택 사항) 메모리 꼭 맞게 줄이기
    // c.buf.shrink_to_fit(); 
    
    return c;
}


struct Node {
    NodeData data;
    std::vector<Node*> next;
    std::vector<size_t> span;
    Node(int level) : next(level, nullptr), span(level, 0), data(GapNode{}) {}
    size_t content_size() const {
        return std::visit([](auto const& n) { return n.size(); }, data);
    }
};

class BiModalText {
public:
    BiModalText() {
        head = new Node(MAX_LEVEL);
        total_size = 0;
        std::random_device rd;
        gen = std::mt19937(rd());
        dist = std::uniform_real_distribution<>(0.0, 1.0);
    }

    ~BiModalText() {
        Node* curr = head;
        while (curr) {
            Node* next = curr->next[0];
            delete curr;
            curr = next;
        }
    }

    // --- [Move Up] Iterator Definition & Smart Caching ---
    class Iterator {
        const Node* curr_node;
        size_t offset;
        
        // 캐싱 변수 (읽기 최적화용)
        const char* cached_ptr;
        bool is_compact;

        void update_cache() {
            if (!curr_node) {
                cached_ptr = nullptr;
                is_compact = false;
                return;
            }
            if (std::holds_alternative<CompactNode>(curr_node->data)) {
                is_compact = true;
                const auto& c_node = std::get<CompactNode>(curr_node->data);
                cached_ptr = c_node.buf.data();
            } else {
                is_compact = false;
                cached_ptr = nullptr;
            }
        }

    public:
        Iterator(const Node* node, size_t off) : curr_node(node), offset(off) {
            update_cache();
        }

        char operator*() const {
            if (!curr_node) return '\0';
            // Compact 모드면 포인터 직접 접근 (Fast Path)
            if (is_compact) return cached_ptr[offset];
            // Gap 모드면 기존 방식 (Slow Path)
            return std::visit([this](auto const& n) { return n.at(offset); }, curr_node->data);
        }

        Iterator& operator++() {
            if (!curr_node) return *this;
            
            // 크기 확인 시에도 최적화
            size_t len = is_compact ? 
                         std::get<CompactNode>(curr_node->data).buf.size() : 
                         std::visit([](auto const& n) { return n.size(); }, curr_node->data);

            offset++;
            if (offset >= len) {
                curr_node = curr_node->next[0];
                offset = 0;
                update_cache(); // 노드 변경 시 캐시 갱신
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

        if (!target) {
            if (total_size == 0) {
                target = new Node(random_level());
                auto& gap = std::get<GapNode>(target->data);
                gap.insert(0, s);
                
                for (size_t i = 0; i < target->next.size(); ++i) {
                    target->next[i] = nullptr; 
                    head->next[i] = target;
                    head->span[i] = target->content_size(); 
                }
            } else {
                 throw std::runtime_error("Unexpected null target on non-empty list");
            }
        } else {
            if (std::holds_alternative<CompactNode>(target->data)) {
                target->data = expand(std::get<CompactNode>(target->data));
            }
            
            std::get<GapNode>(target->data).insert(node_offset, s);

            for (int i = 0; i < MAX_LEVEL; ++i) {
                if (update[i]) {
                    update[i]->span[i] += s.size();
                }
            }

            if (target->content_size() > NODE_MAX_SIZE) {
                split_node(target, update);
            }
        }
        total_size += s.size();
    }

    char at(size_t pos) const {
        if (pos >= total_size) throw std::out_of_range("Index out of range");
        std::array<Node*, MAX_LEVEL> update;
        std::array<size_t, MAX_LEVEL> rank;
        size_t node_offset = 0;
        
        Node* target = find_node(pos, node_offset, update, rank);
        
        if (!target) throw std::runtime_error("Node structure corruption");
        return std::visit([&](auto const& n) { return n.at(node_offset); }, target->data);
    }

    // Iterator를 타지 않고, 노드 내부 버퍼를 통째로 append 하여 대역폭 활용 극대화
    std::string to_string() const {
        std::string res;
        res.reserve(total_size);
        Node* curr = head->next[0]; // Level 0 순회
        while (curr) {
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
        while (curr) {
            if (std::holds_alternative<GapNode>(curr->data)) {
                curr->data = compact(std::get<GapNode>(curr->data));
            }
            curr = curr->next[0];
        }
    }
    
    size_t size() const { return total_size; }

private:
    static constexpr int MAX_LEVEL = 16;
    static constexpr size_t NODE_MAX_SIZE = 4096; 
    
    Node* head;
    size_t total_size;
    std::mt19937 gen;
    std::uniform_real_distribution<> dist;

    int random_level() {
        int lvl = 1;
        while (dist(gen) < 0.5 && lvl < MAX_LEVEL) lvl++;
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
                target = target->next[0];
                node_offset = pos - accumulated;
            }
        }
        return target;
    }

    void split_node(Node* u, std::array<Node*, MAX_LEVEL>& update) {
        auto& u_gap = std::get<GapNode>(u->data);
        std::string full_str = u_gap.to_string(); 
        
        size_t split_point = full_str.size() / 2;
        std::string s1 = full_str.substr(0, split_point);
        std::string s2 = full_str.substr(split_point);
        size_t v_size = s2.size();

        u_gap = GapNode(128); 
        u_gap.insert(0, s1);

        int u_levels = u->next.size();
        int new_level = random_level();
        if (new_level > u_levels) new_level = u_levels; 

        Node* v = new Node(new_level);
        auto& v_gap = std::get<GapNode>(v->data);
        v_gap.insert(0, s2);

        for (int i = 0; i < MAX_LEVEL; ++i) {
            if (!update[i] || update[i]->next[i] != u) continue;
            
            update[i]->span[i] -= v_size;

            if (i < new_level) {
                v->next[i] = u->next[i];
                u->next[i] = v;
                v->span[i] = u->span[i]; 
                u->span[i] = v_size;    
            } else {
                u->span[i] += v_size;
            }
        }
    }
};