#include <cassert>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "BiModalSkipList.hpp"

using namespace std;

// -----------------------------------------------------------------------------
// Tester regression helpers
// -----------------------------------------------------------------------------

void check_equal(const string& ref, const BiModalText& txt,
                 const char* where, int step, int seed) {
    if (ref.size() != txt.size()) {
        cerr << "[FAIL] size mismatch at " << where
             << " step=" << step << " seed=" << seed
             << " ref=" << ref.size()
             << " txt=" << txt.size() << "\n";
#ifdef BIMODAL_DEBUG
        txt.debug_verify_spans(cerr);
        txt.debug_dump_structure(cerr);
#endif
        std::exit(1);
    }

    string txt_str = txt.to_string();
    if (txt_str != ref) {
        cerr << "[FAIL] content mismatch at " << where
             << " step=" << step << " seed=" << seed << "\n";

        size_t min_len = min(ref.size(), txt_str.size());
        size_t diff_pos = 0;
        while (diff_pos < min_len && ref[diff_pos] == txt_str[diff_pos])
            ++diff_pos;

        auto slice = [](const string& s, size_t pos, size_t radius) {
            if (s.empty()) return string{};
            size_t start = (pos > radius) ? pos - radius : 0;
            size_t end   = min(s.size(), pos + radius + 1);
            return s.substr(start, end - start);
        };

        cerr << "First diff at index " << diff_pos << "\n";
        cerr << "ref context : \"" << slice(ref, diff_pos, 20) << "\"\n";
        cerr << "txt context : \"" << slice(txt_str, diff_pos, 20) << "\"\n";
#ifdef BIMODAL_DEBUG
        txt.debug_verify_spans(cerr);
        txt.debug_dump_structure(cerr);
#endif
        std::exit(1);
    }

    string via_iter;
    via_iter.reserve(ref.size());
    for (char c : txt) via_iter.push_back(c);

    if (via_iter != ref) {
        cerr << "[FAIL] iterator mismatch at " << where
             << " step=" << step << " seed=" << seed << "\n";

        size_t min_len = min(ref.size(), via_iter.size());
        size_t diff_pos = 0;
        while (diff_pos < min_len && ref[diff_pos] == via_iter[diff_pos])
            ++diff_pos;

        cerr << "First diff at index " << diff_pos << "\n";
#ifdef BIMODAL_DEBUG
        txt.debug_verify_spans(cerr);
        txt.debug_dump_structure(cerr);
#endif
        std::exit(1);
    }

    if (!ref.empty()) {
        if (txt.at(0) != ref[0]) {
            cerr << "[FAIL] at(0) mismatch at " << where
                 << " step=" << step << " seed=" << seed
                 << " txt=" << txt.at(0)
                 << " ref=" << ref[0] << "\n";
#ifdef BIMODAL_DEBUG
            txt.debug_verify_spans(cerr);
            txt.debug_dump_structure(cerr);
#endif
            std::exit(1);
        }
        if (txt.at(ref.size() - 1) != ref.back()) {
            cerr << "[FAIL] at(last) mismatch at " << where
                 << " step=" << step << " seed=" << seed << "\n";
#ifdef BIMODAL_DEBUG
            txt.debug_verify_spans(cerr);
            txt.debug_dump_structure(cerr);
#endif
            std::exit(1);
        }

        mt19937 rng(123456);
        for (int k = 0; k < 10 && ref.size() > 1; ++k) {
            size_t pos = rng() % ref.size();
            char a = txt.at(pos);
            char b = ref[pos];
            if (a != b) {
                cerr << "[FAIL] at(" << pos << ") mismatch at "
                     << where << " step=" << step << " seed=" << seed
                     << " txt=" << a << " ref=" << b << "\n";
#ifdef BIMODAL_DEBUG
                txt.debug_verify_spans(cerr);
                txt.debug_dump_structure(cerr);
#endif
                std::exit(1);
            }
        }
    }
}

void simple_sanity_tests() {
    BiModalText txt;
    string ref;

    txt.insert(0, "hello");
    ref.insert(0, "hello");
    check_equal(ref, txt, "simple/insert-empty", 0, 0);

    txt.insert(ref.size(), " world");
    ref.insert(ref.size(), " world");
    check_equal(ref, txt, "simple/insert-end", 0, 0);

    txt.insert(5, ",");
    ref.insert(5, ",");
    check_equal(ref, txt, "simple/insert-mid", 0, 0);

    txt.erase(5, 1);
    ref.erase(5, 1);
    check_equal(ref, txt, "simple/erase-mid", 0, 0);

    size_t erase_len = 6;
    txt.erase(ref.size() - erase_len, erase_len);
    ref.erase(ref.size() - erase_len, erase_len);
    check_equal(ref, txt, "simple/erase-suffix", 0, 0);

    txt.clear();
    ref.clear();
    check_equal(ref, txt, "simple/clear", 0, 0);

    cout << "  \u2713 simple sanity tests passed\n";
}

void split_merge_stress_test() {
    BiModalText txt;
    string ref;

    const size_t big_len = NODE_MAX_SIZE * 3 + 123;
    string big(big_len, 'x');

    txt.insert(0, big);
    ref.insert(0, big);
    check_equal(ref, txt, "split/big-insert", 0, 0);

    txt.insert(big_len / 2, "MID");
    ref.insert(big_len / 2, "MID");
    check_equal(ref, txt, "split/insert-mid", 0, 0);

    txt.insert(0, "HEAD");
    ref.insert(0, "HEAD");
    check_equal(ref, txt, "split/insert-head", 0, 0);

    txt.insert(ref.size(), "TAIL");
    ref.insert(ref.size(), "TAIL");
    check_equal(ref, txt, "split/insert-tail", 0, 0);

    txt.erase(10, 1000);
    ref.erase(10, 1000);
    check_equal(ref, txt, "split/erase-range1", 0, 0);

    txt.erase(ref.size() / 3, NODE_MAX_SIZE / 2);
    ref.erase(ref.size() / 3, NODE_MAX_SIZE / 2);
    check_equal(ref, txt, "split/erase-range2", 0, 0);

    txt.optimize();
    check_equal(ref, txt, "split/optimize", 0, 0);

    cout << "  \u2713 split/merge stress test passed\n";
}

void random_edit_test(int seed, int ops) {
    BiModalText txt;
    string ref;

    mt19937 rng(seed);
    uniform_int_distribution<int> op_dist(0, 9);
    uniform_int_distribution<int> len_dist(1, 32);
    uniform_int_distribution<int> ch_dist(0, 25);

    for (int step = 0; step < ops; ++step) {
        int op = op_dist(rng);

        if (op <= 5) {
            size_t pos = ref.empty() ? 0 : (rng() % (ref.size() + 1));
            int len = len_dist(rng);
            string s;
            s.reserve(len);
            for (int i = 0; i < len; ++i) {
                s.push_back(static_cast<char>('a' + ch_dist(rng)));
            }

            txt.insert(pos, s);
            ref.insert(pos, s);
            check_equal(ref, txt, "random/insert", step, seed);
        } else if (op <= 7) {
            if (!ref.empty()) {
                size_t pos = rng() % ref.size();
                int len = len_dist(rng);
                if (pos + len > ref.size())
                    len = static_cast<int>(ref.size() - pos);
                if (len > 0) {
                    txt.erase(pos, static_cast<size_t>(len));
                    ref.erase(pos, static_cast<size_t>(len));
                    check_equal(ref, txt, "random/erase", step, seed);
                }
            }
        } else {
            txt.optimize();
            check_equal(ref, txt, "random/optimize", step, seed);
        }
    }

    txt.optimize();
    check_equal(ref, txt, "random/final", ops, seed);

    cout << "  \u2713 random test seed=" << seed
         << " ops=" << ops << " passed\n";
}

void run_regression_suite() {
    cout << "\n[REGRESSION] Running deterministic tests...\n";
    simple_sanity_tests();
    split_merge_stress_test();
    random_edit_test(1, 2000);
    random_edit_test(2, 2000);
    random_edit_test(3, 2000);
    cout << "\u2713 Testing regression suite passed\n";
}

// -----------------------------------------------------------------------------
// Invariant checker + fuzzing harness
// -----------------------------------------------------------------------------

class InvariantChecker {
    const BiModalText& bmt;

public:
    explicit InvariantChecker(const BiModalText& b) : bmt(b) {}

    void check_all() {
        check_size_consistency();
        check_random_access();
        check_iterator_consistency();
    }

    void check_size_consistency() {
        size_t reported_size = bmt.size();
        size_t counted = 0;
        for (auto it = bmt.begin(); it != bmt.end(); ++it) counted++;
        if (reported_size != counted) {
            cerr << "INVARIANT VIOLATION: size() = " << reported_size
                 << " but iterator counted " << counted << " elements\n";
            throw runtime_error("Size consistency check failed");
        }
    }

    void check_random_access() {
        if (bmt.size() == 0) return;
        size_t idx = 0;
        for (auto it = bmt.begin(); it != bmt.end(); ++it, ++idx) {
            char via_iterator = *it;
            char via_at = bmt.at(idx);
            if (via_iterator != via_at) {
                cerr << "INVARIANT VIOLATION at index " << idx
                     << ": iterator='" << via_iterator
                     << "' but at()='" << via_at << "'\n";
                throw runtime_error("Random access consistency failed");
            }
        }
    }

    void check_iterator_consistency() {
        string via_to_string = bmt.to_string();
        string via_iterator;
        for (auto it = bmt.begin(); it != bmt.end(); ++it) via_iterator += *it;
        if (via_to_string != via_iterator) {
            cerr << "INVARIANT VIOLATION:\n"
                 << "  to_string(): \"" << via_to_string << "\"\n"
                 << "  iterator:    \"" << via_iterator << "\"\n";
            throw runtime_error("Iterator consistency failed");
        }
    }
};

enum OpType { OP_INSERT, OP_ERASE, OP_OPTIMIZE, OP_READ };

struct Operation {
    OpType type;
    size_t pos;
    string data;
    size_t len;
};

class Fuzzer {
    mt19937 gen;
    BiModalText bmt;
    string reference;
    vector<Operation> history;

    uniform_int_distribution<> op_dist{0, 3};

public:
    explicit Fuzzer(unsigned seed) : gen(seed) {}

    void run(int iterations, bool verbose = false) {
        cout << "Running fuzzer with " << iterations
             << " iterations (seed=" << gen() << ")...\n";

        for (int i = 0; i < iterations; ++i) {
            OpType op = static_cast<OpType>(op_dist(gen));
            try {
                switch (op) {
                    case OP_INSERT:   do_insert();   break;
                    case OP_ERASE:    do_erase();    break;
                    case OP_OPTIMIZE: do_optimize(); break;
                    case OP_READ:     do_read();     break;
                }

                if (i % 100 == 99) {
                    verify_state();
                    if (verbose && i % 500 == 499) {
                        cout << "  [" << i + 1 << "/" << iterations << "] "
                             << "Size: " << bmt.size()
                             << ", Ops: I=" << count_ops(OP_INSERT)
                             << " E=" << count_ops(OP_ERASE)
                             << " O=" << count_ops(OP_OPTIMIZE) << "\n";
                    }
                }
            } catch (const exception& e) {
                cerr << "\n=== FUZZER CAUGHT BUG at iteration " << i << " ===\n";
                cerr << "Error: " << e.what() << "\n";
                print_recent_history(10);
#ifdef BIMODAL_DEBUG
                try {
                    cerr << "\n[DEBUG] Verifying spans & dumping structure...\n";
                    bmt.debug_verify_spans(cerr);
                    bmt.debug_dump_structure(cerr);
                } catch (...) {
                    cerr << "[DEBUG] Exception during debug verification\n";
                }
#endif
                throw;
            }
        }

        verify_state();
        InvariantChecker(bmt).check_all();
        cout << "\u2713 Fuzzer completed successfully\n";
        print_stats();
    }

private:
    void do_insert() {
        size_t pos = reference.empty() ? 0 : gen() % (reference.size() + 1);

        int len_choice = gen() % 100;
        size_t len;
        if (len_choice < 70) {
            len = 1 + gen() % 10;
        } else if (len_choice < 90) {
            len = 50 + gen() % 200;
        } else {
            len = 1000 + gen() % 3000;
        }

        string data(len, 'A' + (gen() % 26));

        bmt.insert(pos, data);
        reference.insert(pos, data);

        history.push_back({OP_INSERT, pos, data, 0});
    }

    void do_erase() {
        if (reference.empty()) return;

        size_t pos = gen() % reference.size();
        size_t max_len = reference.size() - pos;
        size_t len = max_len > 0 ? (1 + gen() % min<size_t>(100, max_len)) : 0;

        if (len > 0) {
            bmt.erase(pos, len);
            reference.erase(pos, len);
            history.push_back({OP_ERASE, pos, "", len});
        }
    }

    void do_optimize() {
        bmt.optimize();
        history.push_back({OP_OPTIMIZE, 0, "", 0});
    }

    void do_read() {
        if (reference.empty()) return;

        for (int i = 0; i < 5 && !reference.empty(); ++i) {
            size_t pos = gen() % reference.size();
            char expected = reference[pos];
            char actual = bmt.at(pos);
            if (expected != actual) {
#ifdef BIMODAL_DEBUG
                cerr << "[DEBUG] Read mismatch detected in do_read()\n";
                cerr << "  pos=" << pos
                     << " expected='" << expected
                     << "' got='" << actual << "'\n";
                try {
                    InvariantChecker(bmt).check_all();
                } catch (const exception& e) {
                    cerr << "[DEBUG] InvariantChecker failed: " << e.what() << "\n";
                }
                try {
                    bmt.debug_verify_spans(cerr);
                    bmt.debug_dump_structure(cerr);
                } catch (...) {
                    cerr << "[DEBUG] Exception during debug verification\n";
                }
#endif
                throw runtime_error(
                    "Read mismatch at pos " + to_string(pos) +
                    ": expected '" + expected + "', got '" + actual + "'"
                );
            }
        }
        history.push_back({OP_READ, 0, "", 0});
    }

    void verify_state() {
        string actual = bmt.to_string();
        if (actual != reference) {
            cerr << "STATE DIVERGENCE DETECTED\n"
                 << " Reference size: " << reference.size() << "\n"
                 << " Actual size: " << actual.size() << "\n";

            size_t diff_pos = 0;
            size_t min_len = min(reference.size(), actual.size());
            while (diff_pos < min_len && reference[diff_pos] == actual[diff_pos]) {
                diff_pos++;
            }
            cerr << " First diff at: " << diff_pos << "\n";
            if (diff_pos < min_len) {
                cerr << "  ref[" << diff_pos << "]='" << reference[diff_pos] << "'\n";
                cerr << "  act[" << diff_pos << "]='" << actual[diff_pos] << "'\n";
            }

            auto slice = [](const string& s, size_t pos, size_t radius) {
                if (s.empty()) return string{};
                size_t start = (pos > radius) ? pos - radius : 0;
                size_t end   = min(s.size(), pos + radius + 1);
                return s.substr(start, end - start);
            };
            cerr << "  ref context : \"" << slice(reference, diff_pos, 20) << "\"\n";
            cerr << "  act context : \"" << slice(actual, diff_pos, 20) << "\"\n";

            throw runtime_error("State verification failed");
        }

        if (bmt.size() != reference.size()) {
            throw runtime_error(
                "Size mismatch: bmt.size()=" + to_string(bmt.size()) +
                " reference.size()=" + to_string(reference.size())
            );
        }
    }

    int count_ops(OpType type) const {
        return count_if(history.begin(), history.end(),
                        [type](const Operation& op) { return op.type == type; });
    }

    void print_recent_history(int n) const {
        cerr << "\nRecent operations:\n";
        int start = max(0, static_cast<int>(history.size()) - n);
        for (int i = start; i < static_cast<int>(history.size()); ++i) {
            const auto& op = history[i];
            cerr << "  [" << i << "] ";
            switch (op.type) {
                case OP_INSERT:
                    cerr << "INSERT pos=" << op.pos << " len=" << op.data.size();
                    break;
                case OP_ERASE:
                    cerr << "ERASE pos=" << op.pos << " len=" << op.len;
                    break;
                case OP_OPTIMIZE:
                    cerr << "OPTIMIZE";
                    break;
                case OP_READ:
                    cerr << "READ";
                    break;
            }
            cerr << "\n";
        }
    }

    void print_stats() const {
        cout << "\nFuzzing statistics:\n"
             << "  Total operations: " << history.size() << "\n"
             << "  Inserts:   " << count_ops(OP_INSERT) << "\n"
             << "  Erases:    " << count_ops(OP_ERASE) << "\n"
             << "  Optimizes: " << count_ops(OP_OPTIMIZE) << "\n"
             << "  Reads:     " << count_ops(OP_READ) << "\n"
             << "  Final size: " << reference.size() << " chars\n";
    }
};

// -----------------------------------------------------------------------------
// Structural tests from fuzz harness
// -----------------------------------------------------------------------------

void test_split_boundary() {
    cout << "\n[BOUNDARY TEST] Node split at exact NODE_MAX_SIZE...\n";
    BiModalText bmt;

    string chunk1(4096, 'A');
    bmt.insert(0, chunk1);
    assert(bmt.size() == 4096);

    bmt.insert(4096, "B");
    assert(bmt.size() == 4097);
    assert(bmt.at(4096) == 'B');

    cout << "\u2713 Split boundary test passed\n";
}

void test_erase_across_nodes() {
    cout << "\n[CROSS-NODE TEST] Erase spanning multiple nodes...\n";
    BiModalText bmt;

    for (int i = 0; i < 3; ++i) {
        string chunk(3000, 'A' + i);
        bmt.insert(bmt.size(), chunk);
    }

    assert(bmt.size() == 9000);
    bmt.erase(2500, 4000);
    assert(bmt.size() == 5000);

    for (size_t i = 0; i < 2500; ++i) {
        assert(bmt.at(i) == 'A');
    }

    cout << "\u2713 Cross-node erase test passed\n";
}

void test_optimize_with_tiny_nodes() {
    cout << "\n[MERGE TEST] Optimize merging small nodes...\n";
    BiModalText bmt;

    for (int i = 0; i < 10; ++i) {
        string small(100, 'X');
        bmt.insert(bmt.size(), small);
        bmt.optimize();
    }

    assert(bmt.size() == 1000);
    string result = bmt.to_string();
    assert(result == string(1000, 'X'));

    cout << "\u2713 Tiny node merge test passed\n";
}

void run_boundary_tests() {
    cout << "\n[BOUNDARY] Running targeted structural tests...\n";
    test_split_boundary();
    test_erase_across_nodes();
    test_optimize_with_tiny_nodes();
}

// -----------------------------------------------------------------------------
// Main entry
// -----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    cout << "\n";
    cout << "╔══════════════════════════════════════════════════╗\n";
    cout << "║   BiModalText Advanced Fuzzing & Verification    ║\n";
    cout << "╚══════════════════════════════════════════════════╝\n";

    try {
        run_regression_suite();
        run_boundary_tests();

        int iterations = (argc > 1) ? atoi(argv[1]) : 5000;

        cout << "\n" << string(50, '=') << "\n";
        cout << "Starting fuzzing rounds...\n";
        cout << string(50, '=') << "\n";

        Fuzzer fuzzer1(42);
        fuzzer1.run(iterations, true);

        Fuzzer fuzzer2(12345);
        fuzzer2.run(iterations / 2, false);

        cout << "\n[STRESS] High-intensity fuzzing...\n";
        Fuzzer fuzzer3(99999);
        fuzzer3.run(iterations * 2, true);

        cout << "\n" << string(50, '=') << "\n";
        cout << "\033[32m\u2713 ALL TESTS & FUZZING PASSED\033[0m\n";
        cout << string(50, '=') << "\n\n";

        return 0;

    } catch (const exception& e) {
        cerr << "\n\033[31m✗ TESTING FAILED: " << e.what() << "\033[0m\n\n";
        return 1;
    }
}
