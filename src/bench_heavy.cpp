#include <iostream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <limits>

#include "Baselines.hpp"
#include "BiModalSkipList.hpp"

#if __has_include(<ext/rope>)
#include <ext/rope>
using namespace __gnu_cxx;
#define HEAVY_ROPE_AVAILABLE 1
#else
#define HEAVY_ROPE_AVAILABLE 0
#endif

using namespace std;
using namespace std::chrono;

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
constexpr int LARGE_SIZE = 100 * 1024 * 1024;
constexpr int HEAVY_INSERTS = 5000;

template <typename Func>
double run_best_of(Func&& func) {
    double best = numeric_limits<double>::infinity();
    for (int i = 0; i < SCENARIO_REPEATS; ++i) {
        best = min(best, func());
    }
    return best;
}

double bench_vector() {
    return run_best_of([&]() {
        vector<char> v(LARGE_SIZE, 'x');
        size_t mid = v.size() / 2;
        Timer t;
        t.reset();
        for (int i = 0; i < HEAVY_INSERTS; ++i) {
            v.insert(v.begin() + mid, 'A');
        }
        return t.elapsed_ms();
    });
}

double bench_gap() {
    return run_best_of([&]() {
        SimpleGapBuffer gb(LARGE_SIZE + HEAVY_INSERTS);
        gb.insert(0, string(LARGE_SIZE, 'x'));
        gb.move_gap(LARGE_SIZE / 2);
        Timer t;
        t.reset();
        for (int i = 0; i < HEAVY_INSERTS; ++i) {
            gb.insert(gb.size() / 2, 'A');
        }
        return t.elapsed_ms();
    });
}

double bench_piece() {
    return run_best_of([&]() {
        SimplePieceTable pt;
        pt.insert(0, string(LARGE_SIZE, 'x'));
        size_t mid = pt.size() / 2;
        Timer t;
        t.reset();
        for (int i = 0; i < HEAVY_INSERTS; ++i) {
            pt.insert(mid + i, "A");
        }
        return t.elapsed_ms();
    });
}

#if HEAVY_ROPE_AVAILABLE
double bench_rope() {
    return run_best_of([&]() {
        crope r(LARGE_SIZE, 'x');
        size_t mid = r.size() / 2;
        Timer t;
        t.reset();
        for (int i = 0; i < HEAVY_INSERTS; ++i) {
            r.insert(mid, "A");
        }
        return t.elapsed_ms();
    });
}
#endif

double bench_bimodal() {
    return run_best_of([&]() {
        BiModalText bmt;
        string chunk(4096, 'x');
        for (int i = 0; i < 512; ++i) {
            bmt.insert(bmt.size(), chunk);
        }
        bmt.optimize();

        size_t mid = bmt.size() / 2;
        Timer t;
        t.reset();
        for (int i = 0; i < HEAVY_INSERTS; ++i) {
            bmt.insert(mid, "A");
        }
        return t.elapsed_ms();
    });
}

struct BenchEntry {
    string key;
    string label;
    string note;
    function<double()> run;
};

string normalize_key(string key) {
    transform(key.begin(), key.end(), key.begin(),
              [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return key;
}

void print_usage(const vector<BenchEntry>& entries) {
    cout << "Usage: heavy <structure>\n";
    cout << "Available structures:\n";
    for (const auto& entry : entries) {
        cout << "  - " << entry.key << " : " << entry.label
             << " " << entry.note << "\n";
    }
}

int main(int argc, char** argv) {
    vector<BenchEntry> entries = {
        {"vector", "std::vector", "(Shift Hell)", bench_vector},
        {"gap", "SimpleGapBuffer", "(Fastest)", bench_gap},
        {"piecetable", "SimplePieceTable", "(List Walk)", bench_piece},
        {"bimodal", "BiModalText", "(Competitive)", bench_bimodal}
    };

#if HEAVY_ROPE_AVAILABLE
    entries.push_back({"rope", "SGI Rope", "(Consistent)", bench_rope});
#endif

    if (argc != 2) {
        print_usage(entries);
        return 1;
    }

    string key = normalize_key(argv[1]);
    auto it = find_if(entries.begin(), entries.end(),
                      [&](const BenchEntry& e) { return e.key == key; });
    if (it == entries.end()) {
        print_usage(entries);
        return 1;
    }

    cout << "[Scenario C: The Heavy Typer (N="<< (LARGE_SIZE / 1024 / 1024)
         << "MB, Inserts=" << HEAVY_INSERTS << ", best of "
         << SCENARIO_REPEATS << ")]\n";
    cout << "--------------------------------------------------------------\n";
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note\n";
    cout << "--------------------------------------------------------------\n";

    double best = it->run();
    cout << fixed << setprecision(6);
    cout << left << setw(18) << it->label
         << setw(15) << best
         << it->note << "\n";

    return 0;
}
