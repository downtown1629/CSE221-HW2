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

// --- 1. Compact Node ---
struct CompactNode {
    std::vector<char> buf;
    size_t size() const { return buf.size(); }
    char at(size_t i) const { return buf[i]; }
    std::string to_string() const {
        return std::string(buf.begin(), buf.end());
    }
};

// --- 2. Gap Node ---
struct GapNode {
    std::vector<char> buf;
    size_t gap_start;
    size_t gap_end;

    GapNode(size_t capacity = 128) {
        buf.resize(capacity);
        gap_start = 0;
        gap_end = capacity;
    }

    size_t size() const {
        return buf.size() - (gap_end - gap_start);
    }

    size_t physical_index(size_t logical_idx) const {
        if (logical_idx < gap_start) return logical_idx;
        return logical_idx + (gap_end - gap_start);
    }

    char at(size_t i) const {
        return buf[physical_index(i)];
    }

    void move_gap(size_t target_logical_idx) {
        if (target_logical_idx == gap_start) return;

        if (target_logical_idx > gap_start) {
            size_t dist = target_logical_idx - gap_start;
            std::copy(buf.begin() + gap_end, 
                      buf.begin() + gap_end + dist, 
                      buf.begin() + gap_start);
            gap_start += dist;
            gap_end += dist;
        } else {
            size_t dist = gap_start - target_logical_idx;
            std::copy_backward(buf.begin() + target_logical_idx, 
                               buf.begin() + gap_start, 
                               buf.begin() + gap_end);
            gap_start -= dist;
            gap_end -= dist;
        }
    }

    void insert(size_t pos, std::string_view s) {
        move_gap(pos);
        if (gap_end - gap_start < s.size()) {
            expand_buffer(s.size());
        }
        for (char c : s) {
            buf[gap_start++] = c;
        }
    }

    // [Fix C] std::move 후 size() 호출 방지
    void expand_buffer(size_t needed) {
        size_t old_cap = buf.size();
        size_t new_cap = old_cap * 2 + needed;
        size_t back_part_size = old_cap - gap_end;
        
        std::vector<char> new_buf(new_cap);
        std::copy(buf.begin(), buf.begin() + gap_start, new_buf.begin());
        std::copy(buf.begin() + gap_end, buf.end(), new_buf.end() - back_part_size);
        
        buf = std::move(new_buf);
        gap_end = new_cap - back_part_size; 
    }

    std::string to_string() const {
        std::string res;
        res.reserve(size());
        for(size_t i=0; i<gap_start; ++i) res += buf[i];
        for(size_t i=gap_end; i<buf.size(); ++i) res += buf[i];
        return res;
    }
};

using NodeData = std::variant<GapNode, CompactNode>;

GapNode expand(const CompactNode& c) {
    GapNode g(c.buf.size() + 128);
    std::copy(c.buf.begin(), c.buf.end(), g.buf.begin());
    g.gap_start = c.buf.size();
    g.gap_end = g.buf.size();
    return g;
}

CompactNode compact(const GapNode& g) {
    CompactNode c;
    c.buf.reserve(g.size());
    c.buf.insert(c.buf.end(), g.buf.begin(), g.buf.begin() + g.gap_start);
    c.buf.insert(c.buf.end(), g.buf.begin() + g.gap_end, g.buf.end());
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

    void insert(size_t pos, std::string_view s) {
        if (pos > total_size) throw std::out_of_range("Pos out of range");

        std::array<Node*, MAX_LEVEL> update;
        std::array<size_t, MAX_LEVEL> rank;
        size_t node_offset = 0;

        Node* target = find_node(pos, node_offset, update, rank);

        if (!target) {
            // Case 1: Empty List (Initial Insert)
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
            // Case 2: Insert into existing node
            if (std::holds_alternative<CompactNode>(target->data)) {
                target->data = expand(std::get<CompactNode>(target->data));
            }
            
            // [Fix A 효과] find_node에서 이미 offset 보정이 되었으므로 안전하게 삽입
            std::get<GapNode>(target->data).insert(node_offset, s);

            // [Fix B] Insert 시 Span 업데이트 로직 (수학적 정확성 확보)
            for (int i = 0; i < MAX_LEVEL; ++i) {
                if (update[i]->next[i] == target) {
                    // Predecessor -> Target: Target 내부가 커졌으므로, Target -> Next 거리가 멂.
                    target->span[i] += s.size();
                } else {
                    // Predecessor -> (Jump) -> Next: 점프 구간 내에서 Target이 커짐.
                    update[i]->span[i] += s.size();
                }
            }

            if (target->content_size() > NODE_MAX_SIZE) {
                split_node(target);
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
        
        // [Fix A 효과] find_node가 보정한 target을 사용하므로 안전함
        if (!target) throw std::runtime_error("Node structure corruption");
        return std::visit([&](auto const& n) { return n.at(node_offset); }, target->data);
    }

    std::string to_string() const {
        std::string res;
        Node* x = head->next[0];
        while (x) {
            res += std::visit([](auto const& n) { return n.to_string(); }, x->data);
            x = x->next[0];
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

    class Iterator {
        const Node* curr_node;
        size_t offset;
        
    public:
        Iterator(const Node* node, size_t off) : curr_node(node), offset(off) {}

        // 역참조: 현재 위치의 문자 반환
        char operator*() const {
            if (!curr_node) return '\0';
            return std::visit([this](auto const& n) { return n.at(offset); }, curr_node->data);
        }

        // 전위 증가 (++it): 다음 문자로 이동 O(1)
        Iterator& operator++() {
            if (!curr_node) return *this;

            size_t len = curr_node->content_size();
            offset++;

            // 현재 노드의 끝에 도달하면 다음 노드로 이동
            if (offset >= len) {
                curr_node = curr_node->next[0]; // Level 0 링크 타고 이동
                offset = 0;
            }
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return curr_node != other.curr_node || offset != other.offset;
        }
    };

    Iterator begin() const {
        // head는 더미이므로 head->next[0]부터 시작
        return Iterator(head->next[0], 0);
    }

    Iterator end() const {
        return Iterator(nullptr, 0);
    }

private:
    static constexpr int MAX_LEVEL = 16;
    static constexpr size_t NODE_MAX_SIZE = 1024; 
    
    Node* head;
    size_t total_size;
    std::mt19937 gen;
    std::uniform_real_distribution<> dist;

    int random_level() {
        int lvl = 1;
        while (dist(gen) < 0.5 && lvl < MAX_LEVEL) lvl++;
        return lvl;
    }

    // [Fix A: 핵심 수정] 경계값 보정 로직 추가 (Normalization)
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
            
            // [User's Fix] 만약 offset이 노드 크기와 같다면(Append 위치), 
            // 다음 노드의 0번 인덱스로 넘겨주는 것이 안전함. (Split 경계 등)
            // 이를 통해 at() 호출 시 OOB를 방지하고, insert 시에도 다음 노드 앞 삽입으로 유도.
            while (target && node_offset >= target->content_size()) {
                // 단, 마지막 노드의 끝인 경우(Append)는 제외해야 함.
                // target->next[0]이 없으면 루프 종료 (현재 노드 끝에 Append)
                if (!target->next[0]) break;

                accumulated += target->content_size();
                target = target->next[0];
                node_offset = pos - accumulated;
            }
        }
        return target;
    }

    // [Fix B: 구조 무결성] Split 시 Span 재계산 로직
    void split_node(Node* u) {
        auto& u_gap = std::get<GapNode>(u->data);
        std::string full_str = u_gap.to_string(); 
        
        size_t split_point = full_str.size() / 2;
        std::string s1 = full_str.substr(0, split_point);
        std::string s2 = full_str.substr(split_point);
        size_t s1_size = s1.size();

        // 1. U 리셋 (앞부분)
        u_gap = GapNode(128); 
        u_gap.insert(0, s1);

        // 2. V 생성 (뒷부분)
        int u_levels = u->next.size();
        int new_level = random_level();
        if (new_level > u_levels) new_level = u_levels; // 안전장치

        Node* v = new Node(new_level);
        auto& v_gap = std::get<GapNode>(v->data);
        v_gap.insert(0, s2);

        // 3. 토폴로지 및 Span 갱신
        for (int i = 0; i < new_level; ++i) {
            // 연결 변경: U -> Next  ==>  U -> V -> Next
            v->next[i] = u->next[i];
            u->next[i] = v;
            
            // Span 분할:
            // Old: U -> (dist) -> Next
            // New: U -> (s1) -> V -> (dist - s1) -> Next
            
            // 기존 U->span[i]는 (S1 + S2 + dist_to_next) 값이었음.
            size_t old_dist = u->span[i];
            
            // U -> V 구간의 거리는 U의 새로운 크기 (S1)
            u->span[i] = s1_size;
            
            // V -> Next 구간의 거리는 나머지 (Old - S1)
            v->span[i] = old_dist - s1_size;
        }
        
        // V가 존재하지 않는 상위 레벨 (i >= new_level)
        // U -> Next 연결은 유지됨.
        // U->span[i] 값도 (S1 + S2 + dist) 그대로 유지되어야 함. (변경 없음)
    }
};