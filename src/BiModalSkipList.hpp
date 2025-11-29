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

    // [Fix] std::move 후 size() 호출 방지
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

            // [Corrected] Insert 시 Span 업데이트 로직
            // target으로 들어오는 모든 링크(update[i])의 span을 증가시켜야 함.
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

    std::string to_string() const {
        std::string res;
        for(auto c : *this) res += c; // Iterator 사용
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

    // --- Iterator 구현 (Public) ---
    class Iterator {
        const Node* curr_node;
        size_t offset;
    public:
        Iterator(const Node* node, size_t off) : curr_node(node), offset(off) {}

        char operator*() const {
            if (!curr_node) return '\0';
            return std::visit([this](auto const& n) { return n.at(offset); }, curr_node->data);
        }

        Iterator& operator++() {
            if (!curr_node) return *this;
            size_t len = curr_node->content_size();
            offset++;
            if (offset >= len) {
                curr_node = curr_node->next[0];
                offset = 0;
            }
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return curr_node != other.curr_node || offset != other.offset;
        }
    };

    Iterator begin() const { return Iterator(head->next[0], 0); }
    Iterator end() const { return Iterator(nullptr, 0); }

private:
    static constexpr int MAX_LEVEL = 16;
    static constexpr size_t NODE_MAX_SIZE = 4096; // [Tuned] 성능 최적화
    
    Node* head;
    size_t total_size;
    std::mt19937 gen;
    std::uniform_real_distribution<> dist;

    int random_level() {
        int lvl = 1;
        while (dist(gen) < 0.5 && lvl < MAX_LEVEL) lvl++;
        return lvl;
    }

    // [Corrected] find_node 경계 보정 (Normalization)
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
            
            // 경계 보정: offset이 size를 넘어가면 다음 노드로 이동
            while (target && node_offset >= target->content_size()) {
                if (!target->next[0]) break; // Append 위치면 중단
                accumulated += target->content_size();
                target = target->next[0];
                node_offset = pos - accumulated;
            }
        }
        return target;
    }

    // [Corrected] Split 시 Predecessor의 Span 갱신 로직 복구
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
            // update[i]는 u의 predecessor임이 보장됨 (find_node 결과)
            // 단, update[i] -> next[i] 가 u여야 함.
            if (!update[i] || update[i]->next[i] != u) continue;
            
            // 1. Predecessor -> U 거리 보정
            // Insert에서 이미 전체 크기만큼 늘려놨으므로, V만큼 줄여야 U까지 거리가 맞음
            update[i]->span[i] -= v_size;

            if (i < new_level) {
                // Case A: U -> V -> Next
                v->next[i] = u->next[i];
                u->next[i] = v;
                
                v->span[i] = u->span[i]; // U->Next 거리 계승
                u->span[i] = v_size;     // U->V 거리는 V 크기
            } else {
                // Case B: U -> Next (V 건너뜀)
                // U가 작아졌으므로, U -> Next 거리는 V만큼 늘어남 (V를 포함해야 하므로)
                u->span[i] += v_size;
            }
        }
    }
};