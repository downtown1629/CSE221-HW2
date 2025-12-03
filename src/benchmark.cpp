#include <iostream>
#include <vector>
#include <list>
#include <deque>
#include <string>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <random>
#include <cstring> 
#include <algorithm>

// 헤더 파일 포함
#include "BiModalSkipList.hpp"
#include "Baselines.hpp" // [추가됨] Piece Table 분리

// Rope 지원 여부 확인
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
//  Test Parameters
// =========================================================
const int INITIAL_SIZE = 200000;
const int INSERT_COUNT = 20000;
long long dummy_checksum = 0;

// =========================================================
//  Benchmarks
// =========================================================

void bench_vector() {
    vector<char> v(INITIAL_SIZE, 'x');
    Timer t;

    size_t mid = v.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        v.insert(v.begin() + mid, 'A');
    }
    double time_insert = t.elapsed_ms();

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

    size_t mid = d.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        d.insert(d.begin() + mid, 'A');
    }
    double time_insert = t.elapsed_ms();

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

void bench_string() {
    string s(INITIAL_SIZE, 'x');
    Timer t;

    size_t mid = s.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        s.insert(mid, 1, 'A');
        mid++; 
    }
    double time_insert = t.elapsed_ms();

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
    gb.insert(0, string(INITIAL_SIZE, 'x'));
    
    Timer t;
    size_t mid = gb.size() / 2;
    gb.move_gap(mid); 

    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        gb.insert(gb.size() / 2, 'A'); 
    }
    double time_insert = t.elapsed_ms();

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

void bench_piece_table() {
    SimplePieceTable pt;
    pt.insert(0, string(INITIAL_SIZE, 'x'));

    Timer t;
    size_t mid = pt.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        pt.insert(mid + i, "A"); 
    }
    double time_insert = t.elapsed_ms();

    // Piece Table naive read is too slow for large N, report N/A or partial    
    cout << left << setw(18) << "Piece Table" 
         << setw(15) << time_insert 
         << setw(15) << "N/A (O(N^2))" 
         << " (Linked List)" << endl;
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
    for(int i=0; i<INITIAL_SIZE/1000; ++i) bmt.insert(bmt.size(), string(1000, 'x'));
    bmt.optimize(); 

    Timer t;
    size_t mid = bmt.size() / 2;
    
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        bmt.insert(mid + i, "A"); 
    }
    double time_insert = t.elapsed_ms();

    t.reset();
    long long sum = 0;
    bmt.scan([&](char c) { sum += c; });
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    cout << left << setw(18) << "BiModalText" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Target)" << endl;
}

// =========================================================
//  Scenarios
// =========================================================

void bench_heavy_typer() {
    const int LARGE_SIZE = 2 * 1024 * 1024; // 2MB
    const int HEAVY_INSERTS = 5000;
    
    cout << "\n[Scenario C: The Heavy Typer (N=2MB, Inserts=5000)]" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    {
        vector<char> v(LARGE_SIZE, 'x');
        Timer t;
        size_t mid = v.size() / 2;
        for(int i=0; i<HEAVY_INSERTS; ++i) v.insert(v.begin() + mid, 'A');
        cout << left << setw(18) << "std::vector" << setw(15) << t.elapsed_ms() << "(Shift Hell)" << endl;
    }

    {
        SimpleGapBuffer gb(LARGE_SIZE + HEAVY_INSERTS);
        gb.insert(0, string(LARGE_SIZE, 'x'));
        gb.move_gap(LARGE_SIZE / 2); 
        Timer t;
        for(int i=0; i<HEAVY_INSERTS; ++i) gb.insert(gb.size() / 2, 'A');
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << t.elapsed_ms() << "(Fastest)" << endl;
    }

    {
        SimplePieceTable pt;
        pt.insert(0, string(LARGE_SIZE, 'x'));
        Timer t;
        size_t mid = pt.size() / 2;
        for(int i=0; i<HEAVY_INSERTS; ++i) pt.insert(mid + i, "A");
        cout << left << setw(18) << "Piece Table" << setw(15) << t.elapsed_ms() << "(List Walk)" << endl;
    }

#if ROPE_AVAILABLE
    {
        crope r(LARGE_SIZE, 'x');
        Timer t;
        size_t mid = r.size() / 2;
        for(int i=0; i<HEAVY_INSERTS; ++i) r.insert(mid, "A");
        cout << left << setw(18) << "SGI Rope" << setw(15) << t.elapsed_ms() << "(Consistent)" << endl;
    }
#endif

    {
        BiModalText bmt;
        string chunk(4096, 'x');
        for(int i=0; i<512; ++i) bmt.insert(bmt.size(), chunk); // 2MB
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

    {
        vector<char> v(INITIAL_N, 'x');
        size_t pos = v.size() / 2;
        Timer t;
        for(int i=0; i<DELETE_OPS; ++i) v.erase(v.begin() + pos);
        cout << left << setw(18) << "std::vector" << setw(15) << t.elapsed_ms() << "(Shift)" << endl;
    }

    {
        SimplePieceTable pt;
        pt.insert(0, string(INITIAL_N, 'x'));
        size_t pos = pt.size() / 2;
        Timer t;
        for(int i=0; i<DELETE_OPS; ++i) pt.erase(pos, 1);
        cout << left << setw(18) << "Piece Table" << setw(15) << t.elapsed_ms() << "(List Split)" << endl;
    }

#if ROPE_AVAILABLE
    {
        crope r(INITIAL_N, 'x');
        size_t pos = r.size() / 2;
        Timer t;
        for(int i=0; i<DELETE_OPS; ++i) r.erase(pos, 1);
        cout << left << setw(18) << "SGI Rope" << setw(15) << t.elapsed_ms() << "(Tree Rebal)" << endl;
    }
#endif

    {
        BiModalText bmt;
        for(int i=0; i<INITIAL_N/1000; ++i) bmt.insert(0, string(1000, 'x'));
        bmt.optimize();

        size_t pos = bmt.size() / 2;
        Timer t;
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

    {
        string s(N, 'x');
        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % s.size(); 
            sum += s[pos];
            s.insert(pos, "A"); 
        }
        cout << left << setw(18) << "std::string" << setw(15) << t.elapsed_ms() << "(Fast Read)" << endl;
        dummy_checksum += sum;
    }

    {
        SimpleGapBuffer gb(N + ITERATIONS);
        gb.insert(0, string(N, 'x'));
        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % gb.size();
            sum += gb.at(pos); 
            gb.insert(pos, 'A');
        }
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << t.elapsed_ms() << "(Gap Thrash)" << endl;
        dummy_checksum += sum;
    }

    {
        SimplePieceTable pt;
        pt.insert(0, string(N, 'x'));
        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % pt.size();
            sum += pt.at(pos); // O(N) Scan for each read
            pt.insert(pos, "A"); // O(N) Scan for each insert
        }
        cout << left << setw(18) << "Piece Table" << setw(15) << t.elapsed_ms() << "(Slow Search)" << endl;
        dummy_checksum += sum;
    }

#if ROPE_AVAILABLE
    {
        crope r(N, 'x');
        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % r.size();
            sum += r[pos]; 
            r.insert(pos, "A"); 
        }
        cout << left << setw(18) << "SGI Rope" << setw(15) << t.elapsed_ms() << "(Balanced)" << endl;
        dummy_checksum += sum;
    }
#endif

    {
        BiModalText bmt;
        string chunk(1000, 'x');
        for(int i=0; i<N/1000; ++i) bmt.insert(0, chunk);
        bmt.optimize();

        long long sum = 0;
        Timer t;
        for(int i=0; i<ITERATIONS; ++i) {
            size_t pos = (i * 1234) % bmt.size();
            sum += bmt.at(pos); 
            bmt.insert(pos, "A");
        }
        cout << left << setw(18) << "BiModalText" << setw(15) << t.elapsed_ms() << "(Balanced)" << endl;
        dummy_checksum += sum;
    }
}

void bench_random_access() {
    const int TEST_SIZE = 100000; 
    const int RAND_INSERTS = 5000; 
    
    cout << "\n[Scenario: Random Cursor Movement & Insertion]" << endl;
    cout << "  - N=" << TEST_SIZE << ", Inserts=" << RAND_INSERTS << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    mt19937 gen(1234);
    uniform_int_distribution<> dist(0, TEST_SIZE);

    {
        SimpleGapBuffer gb(TEST_SIZE + RAND_INSERTS);
        gb.insert(0, string(TEST_SIZE, 'x'));
        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % gb.size();
            gb.insert(pos, 'A'); 
        }
        cout << left << setw(18) << "SimpleGapBuffer" 
             << setw(15) << t.elapsed_ms() 
             << "(Slow Gap Move)" << endl;
    }

    {
        SimplePieceTable pt;
        pt.insert(0, string(TEST_SIZE, 'x'));
        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % pt.size();
            // Naive Piece Table Search is O(N) -> Total O(M * N)
            pt.insert(pos, "A"); 
        }
        cout << left << setw(18) << "Piece Table" 
             << setw(15) << t.elapsed_ms() 
             << "(O(N) Search)" << endl;
    }

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

    {
        BiModalText bmt;
        for(int i=0; i<TEST_SIZE/1000; ++i) bmt.insert(0, string(1000, 'x'));
        bmt.optimize(); 

        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % bmt.size();
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
    bench_string();
    bench_simple_gap();
    bench_piece_table(); // Added
    bench_rope();
    bench_bimodal();

    bench_heavy_typer();
    bench_deletion();
    bench_mixed_workload();

    bench_random_access();

    if (dummy_checksum == 123456789) cout << ""; 
    return 0;
}