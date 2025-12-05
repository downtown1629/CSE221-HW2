#include <iostream>
#include <vector>
#include <list>
#include <string>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <random>
#include <cstring> 
#include <algorithm>
#include <limits>

// 헤더 파일 포함
#include "BiModalSkipList.hpp"
#include "Baselines.hpp" // Piece Table, Gap Buffer 분리

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
    using clock = std::chrono::steady_clock;
    clock::time_point start;
public:
    Timer() { reset(); }
    void reset() { start = clock::now(); }
    double elapsed_ms() {
        auto end = clock::now();
        return duration_cast<duration<double, milli>>(end - start).count();
    }
};

constexpr int SCENARIO_REPEATS = 10;

template <typename Func>
double run_best_of(Func&& func) {
    double best = std::numeric_limits<double>::infinity();
    for (int attempt = 0; attempt < SCENARIO_REPEATS; ++attempt) {
        best = std::min(best, func());
    }
    return best;
}

struct TypingStats {
    double insert_ms;
    double read_ms;
};

template <typename Func>
TypingStats run_best_typing(Func&& func) {
    TypingStats best{std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::infinity()};
    for (int attempt = 0; attempt < SCENARIO_REPEATS; ++attempt) {
        TypingStats current = func();
        best.insert_ms = std::min(best.insert_ms, current.insert_ms);
        best.read_ms = std::min(best.read_ms, current.read_ms);
    }
    return best;
}

// =========================================================
//  Test Parameters
// =========================================================
const int INITIAL_SIZE = 5 * 1024 * 1024; // 5MB
const int INSERT_COUNT = 1000;
long long dummy_checksum = 0;

// =========================================================
//  Benchmarks
// =========================================================

TypingStats bench_vector_once() {
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

    return {time_insert, time_read};
}

TypingStats bench_string_once() {
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

    return {time_insert, time_read};
}

TypingStats bench_simple_gap_once() {
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

    return {time_insert, time_read};
}

TypingStats bench_piece_table_once() {
    SimplePieceTable pt;
    pt.insert(0, string(INITIAL_SIZE, 'x'));

    const double time_insert = 0.0; // Skip expensive O(N) insert benchmarking

    // Reading (Scan) - 이제 실제로 측정합니다.
    long long sum = 0;
    Timer t;
    t.reset();
    pt.scan([&](char c) { sum += c; }); // scan 메서드 사용
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;
    
    return {time_insert, time_read};
}

#if ROPE_AVAILABLE
TypingStats bench_rope_once() {
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

    return {time_insert, time_read};
}
#endif

TypingStats bench_bimodal_once() {
    BiModalText bmt;
    for(int i=0; i<INITIAL_SIZE/1000; ++i) bmt.insert(bmt.size(), string(1000, 'x'));

    Timer t;
    size_t mid = bmt.size() / 2;
    
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        bmt.insert(mid + i, "A"); 
    }
    double time_insert = t.elapsed_ms();

    // 사용자가 명시적으로 optimize()를 요청하는 시점을 분리한다.
    bmt.optimize();

    t.reset();
    long long sum = 0;
    bmt.scan([&](char c) { sum += c; });
    double time_read = t.elapsed_ms();
    dummy_checksum += sum;

    return {time_insert, time_read};
}


void bench_deletion() {
    const int INITIAL_N = 5 * 1024 * 1024;
    const int DELETE_OPS = 10000; 

    cout << "\n[Scenario D: The Backspacer (Backspace " << DELETE_OPS
         << " times, best of " << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (INITIAL_N / 1024 / 1024) << "MB" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    {
        auto best = run_best_of([&]() {
            vector<char> v(INITIAL_N, 'x');
            size_t pos = v.size() / 2;
            Timer t;
            for(int i=0; i<DELETE_OPS; ++i) v.erase(v.begin() + pos);
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "std::vector" << setw(15) << best << "(Shift)" << endl;
    }

    {
        auto best = run_best_of([&]() {
            SimpleGapBuffer gb(INITIAL_N + DELETE_OPS);
            gb.insert(0, string(INITIAL_N, 'x'));
            size_t pos = gb.size() / 2;
            Timer t;
            for (int i = 0; i < DELETE_OPS; ++i) {
                gb.erase(pos, 1);
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << best << "(Gap Expand)" << endl;
    }

    {
        auto best = run_best_of([&]() {
            SimplePieceTable pt;
            pt.insert(0, string(INITIAL_N, 'x'));
            size_t pos = pt.size() / 2;
            Timer t;
            for(int i=0; i<DELETE_OPS; ++i) pt.erase(pos, 1);
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimplePieceTable" << setw(15) << best << "(List Split)" << endl;
    }

#if ROPE_AVAILABLE
    {
        auto best = run_best_of([&]() {
            crope r(INITIAL_N, 'x');
            size_t pos = r.size() / 2;
            Timer t;
            for(int i=0; i<DELETE_OPS; ++i) r.erase(pos, 1);
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SGI Rope" << setw(15) << best << "(Tree Rebal)" << endl;
    }
#endif

    {
        auto best = run_best_of([&]() {
            BiModalText bmt;
            for(int i=0; i<INITIAL_N/1000; ++i) bmt.insert(0, string(1000, 'x'));
            bmt.optimize();

            size_t pos = bmt.size() / 2;
            Timer t;
            for(int i=0; i<DELETE_OPS; ++i) bmt.erase(pos, 1);
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "BiModalText" << setw(15) << best << "(Gap Expand)" << endl;
    }
}

void bench_mixed_workload() {
    const int N = 5 * 1024 * 1024;
    const int ITERATIONS = 1000; 

    cout << "\n[Scenario E: The Refactorer (" << (N / 1024 / 1024) << "MB Random Read & Edit, best of "
         << SCENARIO_REPEATS << ")]" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    {
        auto best = run_best_of([&]() {
            string s(N, 'x');
            long long sum = 0;
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                size_t pos = (i * 1234) % s.size(); 
                sum += s[pos];
                s.insert(pos, "A"); 
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "std::string" << setw(15) << best << "(O(N) Data Move)" << endl;
    }

    {
        auto best = run_best_of([&]() {
            SimpleGapBuffer gb(N + ITERATIONS);
            gb.insert(0, string(N, 'x'));
            long long sum = 0;
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                size_t pos = (i * 1234) % gb.size();
                sum += gb.at(pos); 
                gb.insert(pos, 'A');
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << best << "(Locality Win)" << endl;
    }

    {
        auto best = run_best_of([&]() {
            SimplePieceTable pt;
            pt.insert(0, string(N, 'x'));
            long long sum = 0;
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                size_t pos = (i * 1234) % pt.size();
                sum += pt.at(pos); // O(N) Scan for each read
                pt.insert(pos, "A"); // O(N) Scan for each insert
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimplePieceTable" << setw(15) << best << "(List Scan)" << endl;
    }

#if ROPE_AVAILABLE
    {
        auto best = run_best_of([&]() {
            crope r(N, 'x');
            long long sum = 0;
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                size_t pos = (i * 1234) % r.size();
                sum += r[pos]; 
                r.insert(pos, "A"); 
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SGI Rope" << setw(15) << best << "(Pointer Chase)" << endl;
    }
#endif

    {
        auto best = run_best_of([&]() {
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
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "BiModalText" << setw(15) << best << "(Optimized LogN)" << endl;
    }
}

void bench_typing_mode() {
    struct TypingRow {
        string label;
        bool measure_insert;
        bool measure_read;
        TypingStats stats;
        string note;
    };

    vector<TypingRow> rows;
    rows.push_back({"std::vector", true, true, run_best_typing(bench_vector_once), "(O(N) shift per insert)"});
    rows.push_back({"std::string", true, true, run_best_typing(bench_string_once), "(Baseline, contiguous)"});
    rows.push_back({"SimpleGapBuffer", true, true, run_best_typing(bench_simple_gap_once), "(Gap move, occasional expand)"});
    rows.push_back({"SimplePieceTable", false, true, run_best_typing(bench_piece_table_once), "(Read only; search O(N))"});
#if ROPE_AVAILABLE
    rows.push_back({"SGI Rope", true, true, run_best_typing(bench_rope_once), "(Tree concat/rebal O(log N))"});
#else
    rows.push_back({"SGI Rope", false, false, TypingStats{0, 0}, "(No rope)"});
#endif
    rows.push_back({"BiModalText", true, true, run_best_typing(bench_bimodal_once), "(Skiplist + gap split/merge)"});

    auto print_section = [&](const string& title, bool show_insert) {
        cout << "\n[Scenario: Typing Mode - " << title
             << " (best of " << SCENARIO_REPEATS << ")]" << endl;
        cout << "  - N=" << (INITIAL_SIZE / 1024 / 1024) << "MB";
        if (show_insert) {
            cout << ", Inserts=" << INSERT_COUNT;
        }
        cout << endl;
        cout << "--------------------------------------------------------------" << endl;
        cout << left << setw(18) << "Structure"
             << setw(15) << (show_insert ? "Insert (ms)" : "Read (ms)")
             << "Note" << endl;
        cout << "--------------------------------------------------------------" << endl;

        cout << fixed << setprecision(6);
        for (const auto& row : rows) {
            cout << left << setw(18) << row.label;
            if (show_insert) {
                if (row.measure_insert) {
                    cout << setw(15) << row.stats.insert_ms;
                } else {
                    cout << setw(15) << "N/A";
                }
            } else {
                if (row.measure_read) {
                    cout << setw(15) << row.stats.read_ms;
                } else {
                    cout << setw(15) << "N/A";
                }
            }
            cout << row.note << endl;
        }
        cout.unsetf(ios::floatfield);
        cout << setprecision(6);
    };

    print_section("Insert", true);
    print_section("Read", false);
}

void bench_random_access() {
    const int TEST_SIZE = 5 * 1024 * 1024; 
    const int RAND_INSERTS = 5000; 
    
    cout << "\n[Scenario: Random Cursor Movement & Insertion (best of "
         << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (TEST_SIZE / 1024 / 1024) << "MB, Inserts=" << RAND_INSERTS << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    mt19937 gen(1234);
    uniform_int_distribution<> dist(0, TEST_SIZE);

    {
        auto best = run_best_of([&]() {
            mt19937 local_gen = gen;
            auto local_dist = dist;
            SimpleGapBuffer gb(TEST_SIZE + RAND_INSERTS);
            gb.insert(0, string(TEST_SIZE, 'x'));
            Timer t;
            for (int i = 0; i < RAND_INSERTS; ++i) {
                size_t pos = local_dist(local_gen) % gb.size();
                gb.insert(pos, 'A'); 
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimpleGapBuffer" 
             << setw(15) << best 
             << "(Slow Gap Move)" << endl;
    }

    {
        auto best = run_best_of([&]() {
            mt19937 local_gen = gen;
            auto local_dist = dist;
            SimplePieceTable pt;
            pt.insert(0, string(TEST_SIZE, 'x'));
            Timer t;
            for (int i = 0; i < RAND_INSERTS; ++i) {
                size_t pos = local_dist(local_gen) % pt.size();
                // Naive Piece Table Search is O(N) -> Total O(M * N)
                pt.insert(pos, "A"); 
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimplePieceTable" 
             << setw(15) << best 
             << "(O(N) Search)" << endl;
    }

#if ROPE_AVAILABLE
    {
        auto best = run_best_of([&]() {
            mt19937 local_gen = gen;
            auto local_dist = dist;
            crope r(TEST_SIZE, 'x');
            Timer t;
            for (int i = 0; i < RAND_INSERTS; ++i) {
                size_t pos = local_dist(local_gen) % r.size();
                r.insert(pos, "A"); 
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SGI Rope" 
             << setw(15) << best 
             << "(O(log N))" << endl;
    }
#endif

    {
        double best_total = std::numeric_limits<double>::infinity();

        for (int attempt = 0; attempt < SCENARIO_REPEATS; ++attempt) {
            mt19937 local_gen = gen;
            auto local_dist = dist;
            BiModalText bmt;
            for(int i=0; i<TEST_SIZE/1000; ++i) bmt.insert(0, string(1000, 'x'));

            Timer edit_timer;
            edit_timer.reset();
            for (int i = 0; i < RAND_INSERTS; ++i) {
                size_t pos = local_dist(local_gen) % bmt.size();
                bmt.insert(pos, "A");
            }
            double edit_ms = edit_timer.elapsed_ms();

            Timer opt_timer;
            opt_timer.reset();
            bmt.optimize();
            double optimize_ms = opt_timer.elapsed_ms();

            long long sum = 0;
            Timer scan_timer;
            scan_timer.reset();
            bmt.scan([&](char c) { sum += c; });
            double scan_ms = scan_timer.elapsed_ms();
            dummy_checksum += sum;

            best_total = std::min(best_total, edit_ms + optimize_ms + scan_ms);
        }

        cout << left << setw(18) << "BiModalText" 
             << setw(15) << best_total 
             << "(Insert+optimize+scan)" << endl;
    }
}

void bench_heavy_typer() {
    const int LARGE_SIZE = 100 * 1024 * 1024; // 5MB
    const int HEAVY_INSERTS = 5000;
    
    cout << "\n[Scenario: The Heavy Typer (best of "
         << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (LARGE_SIZE / 1024 / 1024) << "MB, Inserts=" << HEAVY_INSERTS << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    {
        auto best = run_best_of([&]() {
            vector<char> v(LARGE_SIZE, 'x');
            size_t mid = v.size() / 2;
            Timer t;
            for(int i=0; i<HEAVY_INSERTS; ++i) v.insert(v.begin() + mid, 'A');
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "std::vector" << setw(15) << best << "(O(N) shifts)" << endl;
    }

    {
        auto best = run_best_of([&]() {
            SimpleGapBuffer gb(LARGE_SIZE + HEAVY_INSERTS);
            gb.insert(0, string(LARGE_SIZE, 'x'));
            gb.move_gap(LARGE_SIZE / 2); 
            Timer t;
            for(int i=0; i<HEAVY_INSERTS; ++i) gb.insert(gb.size() / 2, 'A');
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << best << "(Gap move/expand)" << endl;
    }

    {
        auto best = run_best_of([&]() {
            SimplePieceTable pt;
            pt.insert(0, string(LARGE_SIZE, 'x'));
            Timer t;
            size_t mid = pt.size() / 2;
            for(int i=0; i<HEAVY_INSERTS; ++i) pt.insert(mid + i, "A");
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimplePieceTable" << setw(15) << best << "(Node split/join)" << endl;
    }

#if ROPE_AVAILABLE
    {
        auto best = run_best_of([&]() {
            crope r(LARGE_SIZE, 'x');
            Timer t;
            size_t mid = r.size() / 2;
            for(int i=0; i<HEAVY_INSERTS; ++i) r.insert(mid, "A");
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SGI Rope" << setw(15) << best << "(O(log N) rebal)" << endl;
    }
#endif

    {
        auto best = run_best_of([&]() {
            BiModalText bmt;
            string chunk(4096, 'x');
            for(int i=0; i<LARGE_SIZE/static_cast<int>(chunk.size()); ++i) bmt.insert(bmt.size(), chunk); // 5MB
            bmt.optimize(); 

            Timer t;
            size_t mid = bmt.size() / 2;
            for(int i=0; i<HEAVY_INSERTS; ++i) bmt.insert(mid, "A");
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "BiModalText" << setw(15) << best << "(Skiplist + gap split)" << endl;
    }
}

int main() {
    bench_typing_mode();
    bench_heavy_typer();
    bench_deletion();
    bench_mixed_workload();

    bench_random_access();

    if (dummy_checksum == 123456789) cout << ""; 
    return 0;
}
