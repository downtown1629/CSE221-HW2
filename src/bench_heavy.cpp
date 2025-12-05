#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <limits>
#include <chrono>
#include <random>

#include "Baselines.hpp"
#include "BiModalSkipList.hpp"

#if __has_include(<ext/rope>)
#include <ext/rope>
using namespace __gnu_cxx;
#define ROPE_AVAILABLE 1
#else
#define ROPE_AVAILABLE 0
#endif

using namespace std;
using namespace std::chrono;

// Shared timer utility
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
constexpr size_t LARGE_SIZE = 100ull * 1024 * 1024; // 100MB
constexpr int HEAVY_INSERTS = 5000;

template <typename Func>
double run_best_of(Func&& func) {
    double best = std::numeric_limits<double>::infinity();
    for (int attempt = 0; attempt < SCENARIO_REPEATS; ++attempt) {
        best = std::min(best, func());
    }
    return best;
}

double bench_gap() {
    return run_best_of([&]() {
        SimpleGapBuffer gb(LARGE_SIZE + HEAVY_INSERTS);
        gb.insert(0, std::string(LARGE_SIZE, 'x'));
        gb.move_gap(gb.size() / 2);
        Timer t;
        for (int i = 0; i < HEAVY_INSERTS; ++i) gb.insert(gb.size() / 2, 'A');
        return t.elapsed_ms();
    });
}

double bench_piece() {
    return run_best_of([&]() {
        NaivePieceTable pt;
        pt.insert(0, std::string(LARGE_SIZE, 'x'));
        Timer t;
        size_t mid = pt.size() / 2;
        for (int i = 0; i < HEAVY_INSERTS; ++i) pt.insert(mid + i, "A");
        return t.elapsed_ms();
    });
}

#if ROPE_AVAILABLE
double bench_rope() {
    return run_best_of([&]() {
        crope r(LARGE_SIZE, 'x');
        Timer t;
        size_t mid = r.size() / 2;
        for (int i = 0; i < HEAVY_INSERTS; ++i) r.insert(mid, "A");
        return t.elapsed_ms();
    });
}
#endif

double bench_bimodal() {
    return run_best_of([&]() {
        BiModalText bmt;
        bmt.insert(0, std::string(LARGE_SIZE, 'x'));
        bmt.optimize();
        Timer t;
        size_t mid = bmt.size() / 2;
        for (int i = 0; i < HEAVY_INSERTS; ++i) bmt.insert(mid, "A");
        return t.elapsed_ms();
    });
}

int main(int argc, char** argv) {
    struct Entry {
        string key;
        string label;
        string note;
        function<double()> run;
    };

    vector<Entry> entries = {
        {"gap", "SimpleGapBuffer", "(Gap move/expand)", bench_gap},
        {"piecetable", "NaivePieceTable", "(Node split/join)", bench_piece},
        {"bimodal", "BiModalText", "(Skiplist + gap split)", bench_bimodal},
    };
#if ROPE_AVAILABLE
    entries.push_back({"rope", "SGI Rope", "(O(log N) rebal)", bench_rope});
#endif

    auto print_usage = [&]() {
        cout << "Usage: heavy <structure>\n";
        cout << "Available structures:\n";
        for (const auto& e : entries) {
            cout << "  - " << e.key << " : " << e.label << " " << e.note << "\n";
        }
    };

    const Entry* target = nullptr;
    if (argc == 2) {
        string key = argv[1];
        transform(key.begin(), key.end(), key.begin(),
                  [](unsigned char c){ return static_cast<char>(tolower(c)); });
        for (const auto& e : entries) {
            if (e.key == key) { target = &e; break; }
        }
        if (!target) {
            print_usage();
            return 1;
        }
    }

    cout << "[Scenario C: The Heavy Typer (best of "
         << SCENARIO_REPEATS << ")]\n";
    cout << "  - N=" << (LARGE_SIZE / 1024 / 1024) << "MB, Inserts=" << HEAVY_INSERTS << "\n";
    cout << "--------------------------------------------------------------\n";
    cout << left << setw(18) << "Structure" << setw(15) << "Time (ms)" << "Note\n";
    cout << "--------------------------------------------------------------\n";

    cout << fixed << setprecision(6);
    auto run_entry = [&](const Entry& e) {
        double best = e.run();
        cout << left << setw(18) << e.label
             << setw(15) << best
             << e.note << "\n";
    };

    if (target) {
        run_entry(*target);
    } else {
        for (const auto& e : entries) run_entry(e);
    }

    return 0;
}
