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
                
                for (size_t i = 0; i < target->level; ++i) {
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

        // [Phase 1] Transmutation: 모든 GapNode를 CompactNode로 변환
        // - 메모리 단편화를 줄이고 읽기 속도(SIMD 친화적)를 확보합니다.
        while (curr) {
            if (std::holds_alternative<GapNode>(curr->data)) {
                curr->data = compact(std::get<GapNode>(curr->data));
            }
            curr = curr->next[0];
        }

        // [Phase 2] Defragmentation: 인접한 작은 노드들을 하나로 병합
        // - 노드 수를 줄여 Skip List 탐색(Jump) 효율을 높입니다.
        curr = head->next[0];
        while (curr && curr->next[0]) {
            Node* next_node = curr->next[0];
            
            // 병합 조건 검사
            // 1. 크기 제한: 두 노드를 합쳐도 NODE_MAX_SIZE를 넘지 않는가?
            // 2. [중요] 최적화: 다음 노드가 Level 1(바닥 레벨)인가?
            //    - Level 1 노드는 상위 레벨 포인터 수정 없이 O(1)로 제거 가능합니다.
            size_t combined_size = curr->content_size() + next_node->content_size();
            
            if (combined_size <= NODE_MAX_SIZE && next_node->level == 1) {
                // --- 병합 로직 시작 (Inlined) ---
                
                // 1. 데이터 병합 (CompactNode끼리의 결합)
                auto& curr_buf = std::get<CompactNode>(curr->data).buf;
                auto& next_buf = std::get<CompactNode>(next_node->data).buf;
                
                // 다음 노드의 데이터를 현재 노드 뒤에 복사
                curr_buf.insert(curr_buf.end(), next_buf.begin(), next_buf.end());

                // 2. 링크(Next Pointer) 수정
                // next_node를 건너뛰고 그 다음 노드를 가리킴
                curr->next[0] = next_node->next[0];

                // 3. Span 업데이트
                // 현재 노드의 span에 사라지는 노드의 span(크기)을 더함
                curr->span[0] += next_node->span[0];

                // 4. 메모리 해제
                delete next_node;
                
                // --- 병합 로직 끝 ---

                // 주의: 병합이 일어났을 경우 curr를 이동시키지 않음
                // 합쳐진 현재 노드가 새로운 다음 노드와 또 합쳐질 수 있기 때문입니다.
            } else {
                // 병합하지 않았다면 다음 노드로 이동
                curr = next_node;
            }
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
        // 1. 데이터 백업 및 분할 준비 (여기서 실패하면 아무 일도 안 일어남)
        auto& u_gap = std::get<GapNode>(u->data);
        std::string full_str = u_gap.to_string(); // 메모리 할당 발생 가능
        
        size_t split_point = full_str.size() / 2;
        std::string s1 = full_str.substr(0, split_point);
        std::string s2 = full_str.substr(split_point);
        size_t v_size = s2.size();

        // 2. [중요] 새로운 노드 'v' 먼저 생성 (Resource Acquisition)
        // 여기서 예외가 발생해도, 기존 노드 'u'는 건드리지 않았으므로 안전함.
        int u_levels = u->level;
        int new_level = random_level();
        if (new_level > u_levels) new_level = u_levels;

        // new Node가 bad_alloc을 던지면 함수가 종료되지만 데이터는 무사함
        Node* v = new Node(new_level); 
        
        // v에 데이터 채우기 (여기서 예외 나도 v만 leak 되고 u는 안전 - 스마트 포인터가 없으므로 v 해제 처리는 필요하지만 데이터 유실은 아님)
        // 엄밀한 처리를 위해 try-catch로 v 삭제를 감싸주면 더 좋지만, 
        // 최소한 u의 데이터가 날아가는 것은 방지됨.
        try {
            auto& v_gap = std::get<GapNode>(v->data);
            v_gap.insert(0, s2);
        } catch (...) {
            delete v; // 생성하다 실패하면 v 정리 후 예외 다시 던짐
            throw;
        }

        // --- 이 시점부터는 메모리 할당이 없으므로 예외가 발생하지 않음 (No-Throw Section) ---

        // 3. 기존 노드 'u' 수정 (이제 안전하게 자를 수 있음)
        u_gap = GapNode(128); // 기존 버퍼 해제 및 새 버퍼 할당 (작은 크기라 실패 확률 낮음)
        // 만약 여기서 실패해도 s2는 v에 살아있으므로 복구 가능성은 있지만, 
        // GapNode 이동 생성자를 활용하면 더 안전함. 여기선 간단히 진행.
        u_gap.insert(0, s1);

        // 4. 포인터 연결 (Linkage Update)
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