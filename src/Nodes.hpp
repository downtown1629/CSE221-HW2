#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <variant>
#include <cstddef>
#include <algorithm>
#include <cstring>

constexpr size_t DEFAULT_GAP_SIZE = 128;   // 필요시 값 조정 (기존 값 사용)
constexpr size_t NODE_MAX_SIZE = 4096;  // 노드 최대 크기
constexpr size_t NODE_MIN_SIZE = 256;   // 병합 기준 등으로 쓰면 여기

// --- 1. Compact Node ---
struct CompactNode {
    std::vector<char> buf;
    
    CompactNode() = default;

    explicit CompactNode(std::vector<char>&& data) : buf(std::move(data)) {}

    CompactNode(const CompactNode&) = default;
    CompactNode& operator=(const CompactNode&) = default;
    CompactNode(CompactNode&&) noexcept = default;
    CompactNode& operator=(CompactNode&&) noexcept = default;
    ~CompactNode() = default;

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

    // 생성자: 기본 용량 할당
    explicit GapNode(size_t capacity = DEFAULT_GAP_SIZE) {
        if (capacity < DEFAULT_GAP_SIZE) capacity = DEFAULT_GAP_SIZE;
        buf.resize(capacity);
        gap_start = 0;
        gap_end = capacity;
    }

    // Rule of Five
    GapNode(const GapNode&) = default;
    GapNode& operator=(const GapNode&) = default;
    GapNode(GapNode&&) noexcept = default;
    GapNode& operator=(GapNode&&) noexcept = default;
    ~GapNode() = default;

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

        char* ptr = buf.data(); // Raw pointer 획득

        if (target_logical_idx > gap_start) {
            // Gap을 오른쪽으로 이동 (데이터를 왼쪽으로 복사)
            size_t dist = target_logical_idx - gap_start;
            // [최적화] memmove 사용 (Overlap 안전)
            // Destination: 기존 Gap의 시작점 (buf.begin() + gap_start)
            // Source: Gap 바로 뒤의 데이터 (buf.begin() + gap_end)
            std::memmove(ptr + gap_start, ptr + gap_end, dist);
            
            gap_start += dist;
            gap_end += dist;
        } else {
            // Gap을 왼쪽으로 이동 (데이터를 오른쪽으로 복사)
            size_t dist = gap_start - target_logical_idx;
            // Destination: 이동할 위치의 Gap 끝부분 (buf.begin() + gap_end - dist)
            // Source: 이동할 데이터의 시작점 (buf.begin() + target_logical_idx)
            std::memmove(ptr + gap_end - dist, ptr + target_logical_idx, dist);
            
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
        size_t new_cap = std::max(old_cap * 2, old_cap + needed + DEFAULT_GAP_SIZE);
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
// [수정된 버전] GapNode::split_right
    // 불필요한 메모리 낭비를 줄여 Read 속도를 회복합니다.
    GapNode split_right(size_t suffix_len) {
        size_t total_len = size(); // 현재 데이터 크기
        size_t split_idx = total_len - suffix_len; // 분할 기준점 (prefix 길이)

        // 1. Gap을 분할 지점으로 이동 (데이터 정렬)
        move_gap(split_idx);
        
        // --- Right Node (새 노드) 생성 ---
        // 뒷부분 데이터를 가져갑니다.
        GapNode new_node(suffix_len + DEFAULT_GAP_SIZE);
        std::copy(buf.begin() + gap_end, buf.end(), new_node.buf.begin());
        new_node.gap_start = suffix_len;
        new_node.gap_end = new_node.buf.size();

        // --- Left Node (현재 노드) 최적화 [중요!] ---
        // 기존: gap_end = buf.size(); (용량은 그대로 둠 -> 메모리 낭비)
        // 수정: 딱 맞는 크기의 새 버퍼로 교체합니다.
        
        size_t prefix_len = split_idx;
        // Prefix용 새 버퍼 크기: 데이터 길이 + 기본 Gap (편집 여유분)
        // 만약 Read 위주라면 DEFAULT_GAP_SIZE를 더 작게 잡아도 됩니다.
        size_t new_capacity = prefix_len + DEFAULT_GAP_SIZE;
        
        std::vector<char> new_buf(new_capacity);
        
        // 현재 노드의 앞부분 데이터(prefix)만 복사
        std::copy(buf.begin(), buf.begin() + gap_start, new_buf.begin());
        
        // 버퍼 교체 (Swap)
        buf = std::move(new_buf);
        
        // Gap 재설정
        gap_start = prefix_len;
        gap_end = buf.size(); // 끝까지 Gap

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
    GapNode g(c.buf.size() + DEFAULT_GAP_SIZE);
    
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
    
    // 불필요한 용량 제거
    c.buf.shrink_to_fit(); 
    
    return c;
}


struct Node {
    NodeData data;
    
    // 외부에서는 기존처럼 배열로 접근
    Node** next;
    size_t* span;
    int level;

private:
    // [최적화 핵심] 실제 메모리 덩어리를 가리키는 포인터
    char* memory_block;

public:
    Node(int lvl) : data(GapNode{}), level(lvl), memory_block(nullptr) {
        // 1. 크기 계산
        size_t next_size = sizeof(Node*) * lvl;
        size_t span_size = sizeof(size_t) * lvl;
        
        // 2. 단일 할당
        memory_block = new char[next_size + span_size];

        // 3. 포인터 연결
        next = reinterpret_cast<Node**>(memory_block);
        span = reinterpret_cast<size_t*>(memory_block + next_size);

        // 4. [최적화] 한 번에 0으로 초기화 (nullptr == 0 가정)
        // next 배열은 nullptr로, span 배열은 0으로 초기화됨
        std::memset(memory_block, 0, next_size + span_size);
    }

    ~Node() {
        // 할당된 큰 덩어리 하나만 해제하면 됨
        delete[] memory_block;
    }

    // Rule of Five 유지 (복사/이동 금지)
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = delete;
    Node& operator=(Node&&) = delete;

    size_t content_size() const {
        return std::visit([](auto const& n) { return n.size(); }, data);
    }
};