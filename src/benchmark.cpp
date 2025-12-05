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
#include <cctype>

// 헤더 파일 포함
#include "BiModalSkipList.hpp"
#include "Baselines.hpp" // Piece Table, Gap Buffer 분리

// Rope 지원 여부 확인
#if __has_include(<ext/rope>)
    #include <ext/rope>
    using __gnu_cxx::crope;
    #define ROPE_AVAILABLE 1
#else
    #define ROPE_AVAILABLE 0
#endif

#if defined(LIBROPE)
#include "librope_wrapper.cpp"
#define LIBROPE_AVAILABLE 1
#else
#define LIBROPE_AVAILABLE 0
#endif

using namespace std;
using namespace std::chrono;

// Global filters for selective runs
static char g_scenario_filter = '\0'; // 'a'..'g' or 0 for all
static string g_struct_filter;        // "vector", "string", "gap", "piecetable", "sgirope", "librope", "bimodal"

static string normalize_label(const string& label) {
    string key;
    if (label.find("std::vector") == 0) key = "vector";
    else if (label.find("std::string") == 0) key = "string";
    else if (label.find("SimpleGapBuffer") == 0) key = "gap";
    else if (label.find("NaivePieceTable") == 0) key = "piecetable";
    else if (label.find("SGI Rope") == 0) key = "sgirope";
    else if (label.find("librope") == 0) key = "librope";
    else if (label.find("BiModalText") == 0) key = "bimodal";
    else key = label;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return key;
}

static bool allow_struct(const string& label) {
    if (g_struct_filter.empty()) return true;
    return normalize_label(label) == g_struct_filter;
}

inline bool scenario_enabled(char key) {
    return (g_scenario_filter == '\0') || (g_scenario_filter == key);
}

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

struct TypingRow {
    string label;
    bool measure_insert;
    bool measure_read;
    TypingStats stats;
    string insert_note;
    string read_note;
};

// Chunked prefill helper to unify setup across data structures.
template <typename AppendFn>
inline void prefill_chunks(std::size_t total_bytes, std::size_t chunk_size, AppendFn&& append) {
    std::size_t filled = 0;
    while (filled < total_bytes) {
        std::size_t len = std::min(chunk_size, total_bytes - filled);
        append(len);
        filled += len;
    }
}

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
const int INITIAL_SIZE = 10 * 1024 * 1024; // 10MB
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
    NaivePieceTable pt;
    pt.insert(0, string(INITIAL_SIZE, 'x'));

    Timer t;
    size_t mid = pt.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        pt.insert(mid + i, "A");
    }
    double time_insert = t.elapsed_ms();

    // Reading (Scan) - 이제 실제로 측정합니다.
    long long sum = 0;
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
    if (!scenario_enabled('d')) return;
    const int INITIAL_N = 10 * 1024 * 1024;
    const int DELETE_OPS = 10000; 
    bool any = allow_struct("std::vector") || allow_struct("SimpleGapBuffer") || allow_struct("NaivePieceTable")
#if ROPE_AVAILABLE
        || allow_struct("SGI Rope")
#endif
#if LIBROPE_AVAILABLE
        || allow_struct("librope")
#endif
        || allow_struct("BiModalText");
    if (!any) return;

    cout << "\n[Scenario D: The Backspacer (Backspace " << DELETE_OPS
         << " times, best of " << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (INITIAL_N / 1024 / 1024) << "MB" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    if (allow_struct("std::vector")) {
        auto best = run_best_of([&]() {
            vector<char> v(INITIAL_N, 'x');
            size_t pos = v.size() / 2;
            Timer t;
            for(int i=0; i<DELETE_OPS; ++i) v.erase(v.begin() + pos);
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "std::vector" << setw(15) << best << "(Shift)" << endl;
    }

    if (allow_struct("SimpleGapBuffer")) {
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

    if (allow_struct("NaivePieceTable")) {
        auto best = run_best_of([&]() {
            NaivePieceTable pt;
            pt.insert(0, string(INITIAL_N, 'x'));
            size_t pos = pt.size() / 2;
            Timer t;
            for(int i=0; i<DELETE_OPS; ++i) pt.erase(pos, 1);
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "NaivePieceTable" << setw(15) << best << "(List Split)" << endl;
    }

#if ROPE_AVAILABLE
    if (allow_struct("SGI Rope")) {
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
#if LIBROPE_AVAILABLE
    if (allow_struct("librope")) {
        auto best = run_best_of([&]() {
            LibroPe r;
            r.insert(0, string(INITIAL_N, 'x'));
            size_t pos = r.size() / 2;
            Timer t;
            for(int i=0; i<DELETE_OPS; ++i) r.erase(pos, 1);
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "librope" << setw(15) << best << "(Skiplist del)" << endl;
    }
#endif

    if (allow_struct("BiModalText")) {
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
    if (!scenario_enabled('e')) return;
    const int N = 10 * 1024 * 1024;
    const int ITERATIONS = 5000; 
    bool any = allow_struct("std::string") || allow_struct("SimpleGapBuffer") || allow_struct("NaivePieceTable")
#if ROPE_AVAILABLE
        || allow_struct("SGI Rope")
#endif
#if LIBROPE_AVAILABLE
        || allow_struct("librope")
#endif
        || allow_struct("BiModalText");
    if (!any) return;

    cout << "\n[Scenario E: The Refactorer (" << (N / 1024 / 1024)
         << "MB Random Read & Edit, best of " << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - Iterations=" << ITERATIONS << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    if (allow_struct("std::string")) {
        auto best = run_best_of([&]() {
            string s(N, 'x');
            long long sum = 0;
            mt19937 rng(12345);
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                std::uniform_int_distribution<size_t> dist(0, s.size() - 1);
                size_t pos = dist(rng);
                sum += s[pos];
                s.insert(pos, "A"); 
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "std::string" << setw(15) << best << "(O(N) Data Move)" << endl;
    }

    if (allow_struct("SimpleGapBuffer")) {
        auto best = run_best_of([&]() {
            SimpleGapBuffer gb(N + ITERATIONS);
            gb.insert(0, string(N, 'x'));
            long long sum = 0;
            mt19937 rng(12345);
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                std::uniform_int_distribution<size_t> dist(0, gb.size() - 1);
                size_t pos = dist(rng);
                sum += gb.at(pos); 
                gb.insert(pos, 'A');
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << best << "(Locality Win)" << endl;
    }

    if (allow_struct("NaivePieceTable")) {
        auto best = run_best_of([&]() {
            NaivePieceTable pt;
            pt.insert(0, string(N, 'x'));
            long long sum = 0;
            mt19937 rng(12345);
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                std::uniform_int_distribution<size_t> dist(0, pt.size() - 1);
                size_t pos = dist(rng);
                sum += pt.at(pos); // O(N) Scan for each read
                pt.insert(pos, "A"); // O(N) Scan for each insert
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "NaivePieceTable" << setw(15) << best << "(List Scan)" << endl;
    }

#if ROPE_AVAILABLE
    if (allow_struct("SGI Rope")) {
        auto best = run_best_of([&]() {
            crope r(N, 'x');
            long long sum = 0;
            mt19937 rng(12345);
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                std::uniform_int_distribution<size_t> dist(0, r.size() - 1);
                size_t pos = dist(rng);
                sum += r[pos]; 
                r.insert(pos, "A"); 
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SGI Rope" << setw(15) << best << "(Pointer Chase)" << endl;
    }
#endif
#if LIBROPE_AVAILABLE
    if (allow_struct("librope")) {
        auto best = run_best_of([&]() {
            LibroPe r;
            r.insert(0, string(N, 'x'));
            long long sum = 0;
            mt19937 rng(12345);
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                std::uniform_int_distribution<size_t> dist(0, r.size() - 1);
                size_t pos = dist(rng);
                // LibroPe scan is via full copy; we sample a char by scanning the whole string
                // to avoid missing API. Approximate by inserting without reading.
                r.insert(pos, "A");
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "librope" << setw(15) << best << "(C rope, skiplist)" << endl;
    }
#endif

    if (allow_struct("BiModalText")) {
        auto best = run_best_of([&]() {
            BiModalText bmt;
            string chunk(1000, 'x');
            for(int i=0; i<N/1000; ++i) bmt.insert(0, chunk);
            bmt.optimize();

            long long sum = 0;
            mt19937 rng(12345);
            Timer t;
            for(int i=0; i<ITERATIONS; ++i) {
                std::uniform_int_distribution<size_t> dist(0, bmt.size() - 1);
                size_t pos = dist(rng);
                sum += bmt.at(pos); 
                bmt.insert(pos, "A");
            }
            dummy_checksum += sum;
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "BiModalText" << setw(15) << best << "(Optimized LogN)" << endl;
    }
}

vector<TypingRow> compute_typing_rows() {
    vector<TypingRow> rows;
    rows.push_back({"std::vector", true, true, run_best_typing(bench_vector_once),
                    "(Contiguous array, O(N) shift on insert)",
                    "(Contiguous array, sequential scan)"}); 
    rows.push_back({"std::string", true, true, run_best_typing(bench_string_once),
                    "(Baseline contiguous)",
                    "(Baseline contiguous, sequential scan)"});
    rows.push_back({"SimpleGapBuffer", true, true, run_best_typing(bench_simple_gap_once),
                    "(Gap buffer insert around gap)",
                    "(Gap buffer, two-span scan)"});
    rows.push_back({"NaivePieceTable", true, true, run_best_typing(bench_piece_table_once),
                    "(Piece table, O(N) search/split)",
                    "(Piece table, linked scan)"});
#if ROPE_AVAILABLE
    rows.push_back({"SGI Rope", true, true, run_best_typing(bench_rope_once),
                    "(Tree concat/rebal O(log N))",
                    "(Tree traversal, sequential scan)"});
#else
    rows.push_back({"SGI Rope", false, false, TypingStats{0, 0},
                    "(No rope)", "(No rope)"});
#endif
#if LIBROPE_AVAILABLE
    rows.push_back({"librope", true, true, run_best_typing([]{
        // Typing Insert
        LibroPe r;
        // prefill
        r.insert(0, std::string(INITIAL_SIZE, 'x'));
        Timer t;
        size_t mid = r.size() / 2;
        t.reset();
        for (int i = 0; i < INSERT_COUNT; ++i) {
            r.insert(mid, "A");
        }
        double ins = t.elapsed_ms();
        // read
        long long sum = 0;
        t.reset();
        r.scan([&](char c){ sum += c; });
        double read = t.elapsed_ms();
        dummy_checksum += sum;
        return TypingStats{ins, read};
    }),
    "(C rope, skiplist-based)", "(C rope, skiplist-based scan)"});
#endif
    rows.push_back({"BiModalText", true, true, run_best_typing(bench_bimodal_once),
                    "(Skiplist + gap split/merge)",
                    "(Skiplist nodes, span scan)"});
    vector<TypingRow> filtered;
    for (auto& r : rows) {
        if (allow_struct(r.label)) filtered.push_back(std::move(r));
    }
    return filtered;
}

void bench_typing_insert(const vector<TypingRow>& rows) {
    if (rows.empty()) return;
    cout << "\n[Scenario A: Typing Mode - Insert (best of " << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (INITIAL_SIZE / 1024 / 1024) << "MB, Inserts=" << INSERT_COUNT << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure"
         << setw(15) << "Insert (ms)"
         << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    cout << fixed << setprecision(6);
    for (const auto& row : rows) {
        cout << left << setw(18) << row.label;
        if (row.measure_insert) {
            cout << setw(15) << row.stats.insert_ms;
        } else {
            cout << setw(15) << "N/A";
        }
        cout << row.insert_note << endl;
    }
    cout.unsetf(ios::floatfield);
    cout << setprecision(6);
}

void bench_typing_read(const vector<TypingRow>& rows) {
    const int READ_SIZE = 100 * 1024 * 1024; // 100MB for read-only scan
    if (rows.empty()) return;
    cout << "\n[Scenario B: Sequential Read (best of " << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (READ_SIZE / 1024 / 1024) << "MB" << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure"
         << setw(15) << "Read (ms)"
         << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    cout << fixed << setprecision(6);
    for (const auto& row : rows) {
        cout << left << setw(18) << row.label;
        if (row.measure_read) {
            cout << setw(15) << row.stats.read_ms;
        } else {
            cout << setw(15) << "N/A";
        }
        cout << row.read_note << endl;
    }
    cout.unsetf(ios::floatfield);
    cout << setprecision(6);
}

void bench_random_access() {
    if (!scenario_enabled('f')) return;
    const int TEST_SIZE = 10 * 1024 * 1024; 
    const int RAND_INSERTS = 10000; 
    bool any = allow_struct("SimpleGapBuffer") || allow_struct("NaivePieceTable")
#if ROPE_AVAILABLE
        || allow_struct("SGI Rope")
#endif
#if LIBROPE_AVAILABLE
        || allow_struct("librope")
#endif
        || allow_struct("BiModalText");
    if (!any) return;
    
    cout << "\n[Scenario F: Random Cursor Movement & Insertion (best of "
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
        if (allow_struct("SimpleGapBuffer")) {
            cout << left << setw(18) << "SimpleGapBuffer" 
                 << setw(15) << best 
                 << "(Slow Gap Move)" << endl;
        }
    }

    {
        auto best = run_best_of([&]() {
            mt19937 local_gen = gen;
            auto local_dist = dist;
            NaivePieceTable pt;
            pt.insert(0, string(TEST_SIZE, 'x'));
            Timer t;
            for (int i = 0; i < RAND_INSERTS; ++i) {
                size_t pos = local_dist(local_gen) % pt.size();
                // Naive Piece Table Search is O(N) -> Total O(M * N)
                pt.insert(pos, "A"); 
            }
            return t.elapsed_ms();
        });
        if (allow_struct("NaivePieceTable")) {
            cout << left << setw(18) << "NaivePieceTable" 
                 << setw(15) << best 
                 << "(O(N) Search)" << endl;
        }
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
        if (allow_struct("SGI Rope")) {
            cout << left << setw(18) << "SGI Rope" 
                 << setw(15) << best 
                 << "(O(log N))" << endl;
        }
    }
#endif
#if LIBROPE_AVAILABLE
    {
        auto best = run_best_of([&]() {
            mt19937 local_gen = gen;
            auto local_dist = dist;
            LibroPe r;
            r.insert(0, string(TEST_SIZE, 'x'));
            Timer t;
            for (int i = 0; i < RAND_INSERTS; ++i) {
                size_t pos = local_dist(local_gen) % r.size();
                r.insert(pos, "A");
            }
            return t.elapsed_ms();
        });
        if (allow_struct("librope")) {
            cout << left << setw(18) << "librope" 
                 << setw(15) << best 
                 << "(C rope, skiplist)" << endl;
        }
    }
#endif

    if (allow_struct("BiModalText")) {
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
    if (!scenario_enabled('c')) return;
    const size_t LARGE_SIZE = 100ull * 1024 * 1024; // 100MB
    const int HEAVY_INSERTS = 5000;
    bool any = allow_struct("SimpleGapBuffer") || allow_struct("NaivePieceTable")
#if ROPE_AVAILABLE
        || allow_struct("SGI Rope")
#endif
#if LIBROPE_AVAILABLE
        || allow_struct("librope")
#endif
        || allow_struct("BiModalText");
    if (!any) return;
    
    cout << "\n[Scenario C: The Heavy Typer (best of "
         << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (LARGE_SIZE / 1024 / 1024) << "MB, Inserts=" << HEAVY_INSERTS << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    if (allow_struct("SimpleGapBuffer")) {
        auto best = run_best_of([&]() {
            SimpleGapBuffer gb(LARGE_SIZE + HEAVY_INSERTS);
            gb.insert(0, std::string(LARGE_SIZE, 'x'));
            gb.move_gap(gb.size() / 2); 
            Timer t;
            for(int i=0; i<HEAVY_INSERTS; ++i) gb.insert(gb.size() / 2, 'A');
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << best << "(Gap move/expand)" << endl;
    }

    if (allow_struct("NaivePieceTable")) {
        auto best = run_best_of([&]() {
            NaivePieceTable pt;
            pt.insert(0, std::string(LARGE_SIZE, 'x'));
            Timer t;
            size_t mid = pt.size() / 2;
            for(int i=0; i<HEAVY_INSERTS; ++i) pt.insert(mid + i, "A");
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "NaivePieceTable" << setw(15) << best << "(Node split/join)" << endl;
    }

#if ROPE_AVAILABLE
    if (allow_struct("SGI Rope")) {
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
#if LIBROPE_AVAILABLE
    if (allow_struct("librope")) {
        auto best = run_best_of([&]() {
            LibroPe r;
            r.insert(0, std::string(LARGE_SIZE, 'x'));
            Timer t;
            size_t mid = r.size() / 2;
            for(int i=0; i<HEAVY_INSERTS; ++i) r.insert(mid, "A");
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "librope" << setw(15) << best << "(C rope, skiplist)" << endl;
    }
#endif

    if (allow_struct("BiModalText")) {
        auto best = run_best_of([&]() {
            BiModalText bmt;
            bmt.insert(0, std::string(LARGE_SIZE, 'x'));
            bmt.optimize(); // 준비 단계에서 정리
            Timer t;
            size_t mid = bmt.size() / 2;
            for(int i=0; i<HEAVY_INSERTS; ++i) bmt.insert(mid, "A");
            return t.elapsed_ms();
        });
        if (allow_struct("BiModalText")) {
            cout << left << setw(18) << "BiModalText" << setw(15) << best << "(Skiplist + gap split)" << endl;
        }
    }
}

void bench_paster() {
    if (!scenario_enabled('g')) return;
    const size_t INIT_SIZE = 10ull * 1024 * 1024; // 10MB
    const size_t CHUNK_SIZE = 5ull * 1024 * 1024; // 5MB
    const int REPEATS = 10; // total 500MB insert
    const string big_chunk(CHUNK_SIZE, 'A');

    cout << "\n[Scenario G: The Paster (best of "
         << SCENARIO_REPEATS << ")]" << endl;
    cout << "  - N=" << (INIT_SIZE / 1024 / 1024) << "MB, "
         << "Chunk=" << (CHUNK_SIZE / 1024 / 1024) << "MB x " << REPEATS << endl;
    cout << "--------------------------------------------------------------" << endl;
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    if (allow_struct("SimpleGapBuffer")) {
        auto best = run_best_of([&]() {
            SimpleGapBuffer gb(INIT_SIZE + CHUNK_SIZE * REPEATS);
            gb.insert(0, string(INIT_SIZE, 'x'));
            Timer t;
            for (int i = 0; i < REPEATS; ++i) {
                gb.insert(gb.size() / 2, big_chunk);
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SimpleGapBuffer" << setw(15) << best << "(Gap realloc/memmove)" << endl;
    }

    if (allow_struct("NaivePieceTable")) {
        auto best = run_best_of([&]() {
            NaivePieceTable pt;
            pt.insert(0, string(INIT_SIZE, 'x'));
            Timer t;
            size_t pos = pt.size() / 2;
            for (int i = 0; i < REPEATS; ++i) {
                pt.insert(pos, big_chunk);
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "NaivePieceTable" << setw(15) << best << "(Pointer append)" << endl;
    }

#if ROPE_AVAILABLE
    if (allow_struct("SGI Rope")) {
        auto best = run_best_of([&]() {
            crope r(INIT_SIZE, 'x');
            Timer t;
            size_t pos = r.size() / 2;
            for (int i = 0; i < REPEATS; ++i) {
                r.insert(pos, big_chunk.c_str());
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "SGI Rope" << setw(15) << best << "(Tree split/rebal)" << endl;
    }
#endif

#if LIBROPE_AVAILABLE
    if (allow_struct("librope")) {
        auto best = run_best_of([&]() {
            LibroPe r;
            r.insert(0, string(INIT_SIZE, 'x'));
            Timer t;
            size_t pos = r.size() / 2;
            for (int i = 0; i < REPEATS; ++i) {
                r.insert(pos, big_chunk);
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "librope" << setw(15) << best << "(C rope, skiplist)" << endl;
    }
#endif

    if (allow_struct("BiModalText")) {
        auto best = run_best_of([&]() {
            BiModalText bmt;
            bmt.insert(0, string(INIT_SIZE, 'x'));
            Timer t;
            size_t pos = bmt.size() / 2;
            for (int i = 0; i < REPEATS; ++i) {
                bmt.insert(pos, big_chunk);
            }
            return t.elapsed_ms();
        });
        cout << left << setw(18) << "BiModalText" << setw(15) << best << "(Skiplist + gap split)" << endl;
    }
}

int main(int argc, char** argv) {
    if (argc >= 2) {
        string arg1 = argv[1];
        std::transform(arg1.begin(), arg1.end(), arg1.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (arg1.size() == 1 && arg1[0] >= 'a' && arg1[0] <= 'g') {
            g_scenario_filter = arg1[0];
        } else {
            g_struct_filter = arg1;
        }
    }
    if (argc >= 3) {
        string arg2 = argv[2];
        std::transform(arg2.begin(), arg2.end(), arg2.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        g_struct_filter = arg2;
    }

    auto typing_rows = compute_typing_rows();
    if (scenario_enabled('a')) bench_typing_insert(typing_rows);
    if (scenario_enabled('b')) bench_typing_read(typing_rows);
    bench_heavy_typer();
    bench_deletion();
    bench_mixed_workload();
    bench_random_access();
    bench_paster();
    if (dummy_checksum == 123456789) cout << ""; 
    return 0;
}
