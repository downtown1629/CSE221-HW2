#pragma once

#include <array>
#include <random>
#include <cassert>
#include "Nodes.hpp"   // 방금 만든 노드 헤더

// 스킵 리스트용 상수들
constexpr int MAX_LEVEL = 16;
constexpr double P = 0.25;


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
        clear();
    }

    // 복사/대입 금지 (Node 구조를 복사하려면 deep copy가 필요 → 비현실적)
    BiModalText(const BiModalText&) = delete;
    BiModalText& operator=(const BiModalText&) = delete;

    // 이동도 금지 (skiplist pointer graph 이동은 위험 부담 큼)
    BiModalText(BiModalText&&) noexcept = delete;
    BiModalText& operator=(BiModalText&&) noexcept = delete;

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

        
    void clear() {
        Node* curr = head;
        while (curr) {
            Node* next = curr->next[0];
            delete curr;
            curr = next;
        }
        head = nullptr;
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
    }

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
        // 1) 현재 노드의 GapNode를 가져온다.
        auto& u_gap = std::get<GapNode>(u->data);

        // 전체 논리 길이
        size_t total_len = u_gap.size();
        if (total_len == 0) return; // 안전장치 (실제로는 NODE_MAX_SIZE 초과에서만 호출될 것)

        // 왼쪽/오른쪽을 반반으로 나누는 기준
        size_t split_point = total_len / 2;          // prefix 길이
        size_t suffix_len  = total_len - split_point; // 오른쪽 노드에 들어갈 길이
        size_t v_size      = suffix_len;

        // 2) GapNode::split_right를 이용해 오른쪽 부분을 잘라 새 GapNode를 만든다.
        //    - u_gap: prefix만 남도록 내부 버퍼/갭이 재구성됨
        //    - right_gap: suffix 데이터를 담은 새 GapNode (적절한 capacity 포함)
        GapNode right_gap = u_gap.split_right(suffix_len);

        // 3) 새 노드 v를 만들고 right_gap을 data로 넣어준다.
        int u_levels = static_cast<int>(u->next.size());
        int new_level = random_level();
        if (new_level > u_levels) new_level = u_levels;

        Node* v = new Node(new_level);
        v->data = std::move(right_gap); // NodeData(std::variant)에 GapNode를 이동 대입

        // 4) 스킵 리스트 포인터/스팬(span) 업데이트
        for (int i = 0; i < MAX_LEVEL; ++i) {
            if (!update[i] || update[i]->next[i] != u) continue;

            // 기존에 update[i] -> u 로 가던 span에서,
            // 오른쪽으로 빠진 v_size 만큼을 빼준다.
            update[i]->span[i] -= v_size;

            if (i < new_level) {
                // 이 레벨에서는 u 뒤에 v를 끼워 넣는다.
                v->next[i]  = u->next[i];
                u->next[i]  = v;

                // u가 원래 건너뛰던 span을 v가 가져가고,
                // u는 이제 자기 오른쪽(v)의 길이만큼 span을 갖는다.
                v->span[i] = u->span[i];
                u->span[i] = v_size;
            } else {
                // 이 레벨에서는 v가 연결되지 않으므로,
                // u가 담당해야 할 전체 span이 커진다.
                u->span[i] += v_size;
            }
        }
    }
    
    void remove_node(Node* target, const std::array<Node*, MAX_LEVEL>& update) {
        for (int i = 0; i < MAX_LEVEL; ++i) {
            if (update[i]->next[i] == target) {
                update[i]->next[i] = target->next[i];
                update[i]->span[i] += target->span[i];
            }
        }
        delete target;
    }

};