#include <iostream>
#include <vector>
#include <list>
#include <deque>
#include <string>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <random>
#include <cstring> // for memmove
#include <algorithm> // for max

// BiModalSkipList 헤더 포함
#include "BiModalSkipList.hpp"

// Rope 지원 여부 확인 (Mac/Linux 호환성)
#if __has_include(<ext/rope>)
    #include <ext/rope>
    using namespace __gnu_cxx;
    #define ROPE_AVAILABLE 1
#else
    #define ROPE_AVAILABLE 0
#endif

using namespace std;
using namespace std::chrono;

// =========================================================
//  Utility: Timer
// =========================================================
class Timer {
    high_resolution_clock::time_point start;
public:
    Timer() { reset(); }
    void reset() { start = high_resolution_clock::now(); }
    double elapsed_ms() {
        auto end = high_resolution_clock::now();
        return duration_cast<duration<double, milli>>(end - start).count();
    }
};

// =========================================================
//  Utility: Simple Gap Buffer (Baseline Implementation)
// =========================================================
class SimpleGapBuffer {
    std::vector<char> buf;
    size_t gap_start;
    size_t gap_end;

public:
    SimpleGapBuffer(size_t initial_size = 1024) {
        buf.resize(initial_size);
        gap_start = 0;
        gap_end = initial_size;
    }

    size_t size() const { return buf.size() - (gap_end - gap_start); }

    // Gap 이동 (memmove 최적화 적용)
    void move_gap(size_t pos) {
        if (pos == gap_start) return;
        char* ptr = buf.data();
        
        if (pos > gap_start) {
            size_t dist = pos - gap_start;
            // Gap 뒤의 데이터를 앞으로 당김
            std::memmove(ptr + gap_start, ptr + gap_end, dist);
            gap_start += dist;
            gap_end += dist;
        } else {
            size_t dist = gap_start - pos;
            // Gap 앞의 데이터를 뒤로 밈
            std::memmove(ptr + gap_end - dist, ptr + pos, dist);
            gap_start -= dist;
            gap_end -= dist;
        }
    }

    void insert(size_t pos, char c) {
        if (pos > size()) pos = size();
        move_gap(pos);

        if (gap_start == gap_end) {
            expand(1024);
        }
        buf[gap_start++] = c;
    }
    
    // std::string처럼 문자열 삽입 지원
    void insert(size_t pos, std::string_view s) {
        if (pos > size()) pos = size();
        move_gap(pos);
        
        if (gap_end - gap_start < s.size()) {
            expand(s.size());
        }
        std::memcpy(buf.data() + gap_start, s.data(), s.size());
        gap_start += s.size();
    }

    char at(size_t i) const {
        return (i < gap_start) ? buf[i] : buf[i + (gap_end - gap_start)];
    }

private:
    void expand(size_t needed) {
        size_t old_cap = buf.size();
        size_t new_cap = std::max(old_cap * 2, old_cap + needed);
        std::vector<char> new_buf(new_cap);
        
        size_t back_len = old_cap - gap_end;
        std::memcpy(new_buf.data(), buf.data(), gap_start);
        std::memcpy(new_buf.data() + new_cap - back_len, buf.data() + gap_end, back_len);
        
        buf = std::move(new_buf);
        gap_end = new_cap - back_len;
    }
};

// =========================================================
//  Test Parameters
// =========================================================
const int INITIAL_SIZE = 200000;   // 초기 텍스트 크기
const int INSERT_COUNT = 20000;    // 중간 삽입 횟수 (Typing)
const string CHUNK = "A";

long long dummy_checksum = 0; // 컴파일러 최적화 방지용

// =========================================================
//  Benchmarks
// =========================================================

void bench_vector() {
    vector<char> v(INITIAL_SIZE, 'x');
    Timer t;

    // Typing (Middle Insert)
    size_t mid = v.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        v.insert(v.begin() + mid, 'A');
    }
    double time_insert = t.elapsed_ms();

    // Reading
    long long sum = 0;
    t.reset();
    for (char c : v) sum += c;
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "std::vector" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (O(N) Shift)" << endl;
}

void bench_deque() {
    deque<char> d(INITIAL_SIZE, 'x');
    Timer t;

    // Typing
    size_t mid = d.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        d.insert(d.begin() + mid, 'A');
    }
    double time_insert = t.elapsed_ms();

    // Reading
    long long sum = 0;
    t.reset();
    for (char c : d) sum += c;
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "std::deque" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Fragmented)" << endl;
}

void bench_list() {
    list<char> l(INITIAL_SIZE, 'x');
    
    // Iterator 미리 이동 (검색 시간 제외, 순수 삽입만 측정하기 위해)
    auto it = l.begin();
    advance(it, l.size() / 2);
    
    Timer t;
    // Typing
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        l.insert(it, 'A');
    }
    double time_insert = t.elapsed_ms();

    // Reading
    long long sum = 0;
    t.reset();
    for (char c : l) sum += c;
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "std::list" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Cache Miss)" << endl;
}

void bench_string() {
    string s(INITIAL_SIZE, 'x');
    Timer t;

    // Typing
    size_t mid = s.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        s.insert(mid, 1, 'A');
        mid++; 
    }
    double time_insert = t.elapsed_ms();

    // Reading
    long long sum = 0;
    t.reset();
    for (char c : s) sum += c;
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "std::string" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Baseline)" << endl;
}

void bench_simple_gap() {
    SimpleGapBuffer gb(INITIAL_SIZE + INSERT_COUNT);
    // 초기 더미 데이터 채우기
    gb.insert(0, string(INITIAL_SIZE, 'x'));
    
    Timer t;
    // Typing (한 위치에서 계속 입력 -> Gap 이동 없음 -> 매우 빠름)
    size_t mid = gb.size() / 2;
    gb.move_gap(mid); // 커서 이동 비용 1회

    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        gb.insert(gb.size() / 2, 'A'); // Gap이 이미 거기 있으므로 O(1)
    }
    double time_insert = t.elapsed_ms();

    // Reading
    long long sum = 0;
    t.reset();
    for(size_t i=0; i<gb.size(); ++i) sum += gb.at(i);
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "SimpleGapBuffer" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Ideal Typing)" << endl;
}

void bench_rope() {
#if ROPE_AVAILABLE
    crope r(INITIAL_SIZE, 'x');
    Timer t;
    
    size_t mid = r.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        r.insert(mid, "A");
    }
    double time_insert = t.elapsed_ms();

    long long sum = 0;
    t.reset();
    for (char c : r) sum += c;
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "SGI Rope" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Tree O(logN))" << endl;
#else
    cout << left << setw(18) << "SGI Rope" 
         << setw(15) << "N/A" 
         << setw(15) << "N/A" 
         << " (Not supported)" << endl;
#endif
}

void bench_bimodal() {
    BiModalText bmt;
    // 초기 데이터
    for(int i=0; i<INITIAL_SIZE/1000; ++i) bmt.insert(bmt.size(), string(1000, 'x'));
    bmt.optimize(); // 공정하게 Compact 모드에서 시작

    Timer t;
    size_t mid = bmt.size() / 2;
    
    // Typing
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        bmt.insert(mid + i, "A"); 
    }
    double time_insert = t.elapsed_ms();

    // Optimize Overhead check
    Timer t_opt;
    bmt.optimize();
    double time_opt = t_opt.elapsed_ms();

    // Reading (Scan 사용)
    long long sum = 0;
    t.reset();
    bmt.scan([&](char c) { sum += c; });
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "BiModalText" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Target)" << "\n" << "(Opt: "
         << time_opt << "ms)" << endl; // <-- 이렇게 추가
}

void bench_heavy_typer() {
    const int LARGE_SIZE = 2 * 1024 * 1024; // 2MB
    const int HEAVY_INSERTS = 5000;
    
    cout << "\n[Scenario C: The Heavy Typer (N=2MB, Inserts=5000)]" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    // 1. std::vector (최악의 케이스)
    {
        vector<char> v(LARGE_SIZE, 'x');
        Timer t;
        size_t mid = v.size() / 2;
        for(int i=0; i<HEAVY_INSERTS; ++i) v.insert(v.begin() + mid, 'A');
        cout << left << setw(18) << "std::vector" << setw(15) << t.elapsed_ms() << "(Shift Hell)" << endl;
    }

    // 2. std::deque (Vector보다 낫지만 여전히 느림)
    {
        deque<char> d(LARGE_SIZE, 'x');
        Timer t;
        size_t mid = d.size() / 2;
        for(int i=0; i<HEAVY_INSERTS; ++i) d.insert(d.begin() + mid, 'A');
        cout << left << setw(18) << "std::deque" << setw(15) << t.elapsed_ms() << "(Still Slow)" << endl;
    }

    // 3. SimpleGapBuffer (이상적인 상황 - 커서 고정)
    {
        SimpleGapBuffer gb(LARGE_SIZE + HEAVY_INSERTS);
        gb.insert(0, string(LARGE_SIZE, 'x'));
        gb.move_gap(LARGE_SIZE / 2); // 미리 이동 (공정한 비교)
        
        Timer t;
        for(int i=0; i<HEAVY_INSERTS; ++i) gb.insert(gb.size() / 2, 'A'); // Gap 이동 없음
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << t.elapsed_ms() << "(Fastest)" << endl;
    }

    // 4. Rope
#if ROPE_AVAILABLE
    {
        crope r(LARGE_SIZE, 'x');
        Timer t;
        size_t mid = r.size() / 2;
        for(int i=0; i<HEAVY_INSERTS; ++i) r.insert(mid, "A");
        cout << left << setw(18) << "SGI Rope" << setw(15) << t.elapsed_ms() << "(Consistent)" << endl;
    }
#endif

    // 5. BiModalText
    {
        BiModalText bmt;
        string chunk(1024, 'x');
        for(int i=0; i<2048; ++i) bmt.insert(bmt.size(), chunk); // 2MB
        bmt.optimize(); 

        Timer t;
        size_t mid = bmt.size() / 2;
        for(int i=0; i<HEAVY_INSERTS; ++i) bmt.insert(mid, "A");
        cout << left << setw(18) << "BiModalText" << setw(15) << t.elapsed_ms() << "(Competitive)" << endl;
    }
}

void bench_deletion() {
    const int INITIAL_N = 500000;
    const int DELETE_OPS = 10000; 

    cout << "\n[Scenario D: The Backspacer (Backspace 10k times)]" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    // 1. std::vector
    {
        vector<char> v(INITIAL_N, 'x');
        size_t pos = v.size() / 2;
        Timer t;
        for(int i=0; i<DELETE_OPS; ++i) v.erase(v.begin() + pos);
        cout << left << setw(18) << "std::vector" << setw(15) << t.elapsed_ms() << "(Shift)" << endl;
    }

    // 2. std::list (삭제 최강자)
    {
        list<char> l(INITIAL_N, 'x');
        auto it = l.begin();
        advance(it, l.size() / 2);
        Timer t;
        for(int i=0; i<DELETE_OPS; ++i) it = l.erase(it);
        cout << left << setw(18) << "std::list" << setw(15) << t.elapsed_ms() << "(O(1))" << endl;
    }

    // 3. Rope
#if ROPE_AVAILABLE
    {
        crope r(INITIAL_N, 'x');
        size_t pos = r.size() / 2;
        Timer t;
        // Rope 삭제: erase(pos, len)
        for(int i=0; i<DELETE_OPS; ++i) r.erase(pos, 1);
        cout << left << setw(18) << "SGI Rope" << setw(15) << t.elapsed_ms() << "(Tree Rebal)" << endl;
    }
#endif

    // 4. BiModalText
    {
        BiModalText bmt;
        for(int i=0; i<INITIAL_N/1000; ++i) bmt.insert(0, string(1000, 'x'));
        bmt.optimize();

        size_t pos = bmt.size() / 2;
        Timer t;
        // BiModal 삭제: erase(pos, len)
        for(int i=0; i<DELETE_OPS; ++i) bmt.erase(pos, 1);
        cout << left << setw(18) << "BiModalText" << setw(15) << t.elapsed_ms() << "(Gap Expand)" << endl;
    }
}

void bench_mixed_workload() {
    const int N = 200000;
    const int ITERATIONS = 1000; 

    cout << "\n[Scenario E: The Refactorer (Random Read & Edit)]" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    // 1. std::string (비교 기준)
    {
        string s(N, 'x');
        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % s.size(); // Random-like
            char c = s[pos]; // Read O(1)
            sum += c;
            s.insert(pos, "A"); // Write O(N)
        }
        cout << left << setw(18) << "std::string" << setw(15) << t.elapsed_ms() << "(Fast Read)" << endl;
        dummy_checksum += sum;
    }

    // 2. SimpleGapBuffer (이동 비용 큼)
    {
        SimpleGapBuffer gb(N + ITERATIONS);
        gb.insert(0, string(N, 'x'));
        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % gb.size();
            char c = gb.at(pos); // Read O(1)
            sum += c;
            gb.insert(pos, 'A'); // Write O(N) Gap Move
        }
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << t.elapsed_ms() << "(Gap Thrash)" << endl;
        dummy_checksum += sum;
    }

    // 3. Rope
#if ROPE_AVAILABLE
    {
        crope r(N, 'x');
        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % r.size();
            char c = r[pos]; // Read O(log N) - 조금 느릴 수 있음
            sum += c;
            r.insert(pos, "A"); // Write O(log N)
        }
        cout << left << setw(18) << "SGI Rope" << setw(15) << t.elapsed_ms() << "(Balanced)" << endl;
        dummy_checksum += sum;
    }
#endif

    // 4. BiModalText
    {
        BiModalText bmt;
        string chunk(1000, 'x');
        for(int i=0; i<N/1000; ++i) bmt.insert(0, chunk);
        bmt.optimize();

        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % bmt.size();
            // Read: O(log N) via optimized at()
            char c = bmt.at(pos); 
            sum += c;
            // Write: O(log N) search + O(1) insert
            bmt.insert(pos, "A");
        }
        cout << left << setw(18) << "BiModalText" << setw(15) << t.elapsed_ms() << "(Balanced)" << endl;
        dummy_checksum += sum;
    }
}

// =========================================================
//  Scenario: Random Index Insertion (The Real Test)
// =========================================================
void bench_random_access() {
    const int TEST_SIZE = 100000; 
    const int RAND_INSERTS = 5000; // 랜덤 이동 횟수
    
    cout << "\n[Scenario: Random Cursor Movement & Insertion]" << endl;
    cout << "  - N=" << TEST_SIZE << ", Inserts=" << RAND_INSERTS << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    mt19937 gen(1234);
    uniform_int_distribution<> dist(0, TEST_SIZE);

    // 1. SimpleGapBuffer
    {
        SimpleGapBuffer gb(TEST_SIZE + RAND_INSERTS);
        gb.insert(0, string(TEST_SIZE, 'x'));
        
        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % gb.size();
            // Gap 이동 비용(O(N)) 발생
            gb.insert(pos, 'A'); 
        }
        cout << left << setw(18) << "SimpleGapBuffer" 
             << setw(15) << t.elapsed_ms() 
             << "(Slow Gap Move)" << endl;
    }

    // 2. std::vector
    {
        vector<char> v(TEST_SIZE, 'x');
        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % v.size();
            v.insert(v.begin() + pos, 'A'); 
        }
        cout << left << setw(18) << "std::vector" 
             << setw(15) << t.elapsed_ms() 
             << "(O(N) Shift)" << endl;
    }

    // 3. std::deque
    {
        deque<char> d(TEST_SIZE, 'x');
        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % d.size();
            d.insert(d.begin() + pos, 'A'); 
        }
        cout << left << setw(18) << "std::deque" 
             << setw(15) << t.elapsed_ms() 
             << "(O(N) Shift)" << endl;
    }

    // 4. Rope
#if ROPE_AVAILABLE
    {
        crope r(TEST_SIZE, 'x');
        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % r.size();
            r.insert(pos, "A"); 
        }
        cout << left << setw(18) << "SGI Rope" 
             << setw(15) << t.elapsed_ms() 
             << "(O(log N))" << endl;
    }
#endif

    // 5. BiModalText
    {
        BiModalText bmt;
        for(int i=0; i<TEST_SIZE/1000; ++i) bmt.insert(0, string(1000, 'x'));
        bmt.optimize(); 

        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % bmt.size();
            // Search O(log N) + Insert O(1)
            bmt.insert(pos, "A"); 
        }
        cout << left << setw(18) << "BiModalText" 
             << setw(15) << t.elapsed_ms() 
             << "(Log Search)" << endl;
    }
}

int main() {
    cout << "==============================================================" << endl;
    cout << " Benchmark: N=" << INITIAL_SIZE << ", Inserts=" << INSERT_COUNT << " (Typing Mode)" << endl;
    cout << "==============================================================" << endl;
    cout << left << setw(18) << "Structure" 
         << setw(15) << "Insert (ms)" 
         << setw(15) << "Read (ms)" 
         << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    bench_vector();
    bench_deque();
    bench_list();
    bench_string();
    bench_simple_gap();
    bench_rope();
    bench_bimodal();

    bench_heavy_typer();
    bench_deletion();
    bench_mixed_workload();

    bench_random_access();

    // 최적화 방지용 (실제 출력은 안함)
    if (dummy_checksum == 123456789) cout << ""; 

    cout << "==============================================================" << endl;
    return 0;
}