#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "BiModalSkipList.hpp"

namespace {

void print_pass(const std::string& label) {
    std::cout << "[PASS] " << label << '\n';
}

void check_equal(const std::string& reference, const BiModalText& text,
                 const char* where, int step, int seed) {
    if (reference.size() != text.size()) {
        std::cerr << "[FAIL] size mismatch at " << where
                  << " step=" << step << " seed=" << seed
                  << " ref=" << reference.size()
                  << " txt=" << text.size() << std::endl;
#ifdef BIMODAL_DEBUG
        text.debug_verify_spans(std::cerr);
        text.debug_dump_structure(std::cerr);
#endif
        std::exit(1);
    }

    std::string text_str = text.to_string();
    if (text_str != reference) {
        std::cerr << "[FAIL] content mismatch at " << where
                  << " step=" << step << " seed=" << seed << std::endl;

        size_t min_len = std::min(reference.size(), text_str.size());
        size_t diff_pos = 0;
        while (diff_pos < min_len && reference[diff_pos] == text_str[diff_pos]) {
            ++diff_pos;
        }

        std::cerr << "First diff at index " << diff_pos << std::endl;

        auto slice = [](const std::string& s, size_t pos, size_t radius) {
            if (s.empty()) return std::string{};
            size_t start = (pos > radius) ? pos - radius : 0;
            size_t end = std::min(s.size(), pos + radius + 1);
            return s.substr(start, end - start);
        };

        std::cerr << "ref context : \"" << slice(reference, diff_pos, 20)
                  << "\"\n";
        std::cerr << "txt context : \"" << slice(text_str, diff_pos, 20)
                  << "\"\n";
#ifdef BIMODAL_DEBUG
        text.debug_verify_spans(std::cerr);
        text.debug_dump_structure(std::cerr);
#endif
        std::exit(1);
    }

    std::string via_iter;
    via_iter.reserve(reference.size());
    for (char c : text) via_iter.push_back(c);

    if (via_iter != reference) {
        std::cerr << "[FAIL] iterator mismatch at " << where
                  << " step=" << step << " seed=" << seed << std::endl;

        size_t min_len = std::min(reference.size(), via_iter.size());
        size_t diff_pos = 0;
        while (diff_pos < min_len && reference[diff_pos] == via_iter[diff_pos]) {
            ++diff_pos;
        }

        std::cerr << "First diff at index " << diff_pos << std::endl;
#ifdef BIMODAL_DEBUG
        text.debug_verify_spans(std::cerr);
        text.debug_dump_structure(std::cerr);
#endif
        std::exit(1);
    }

    if (!reference.empty()) {
        if (text.at(0) != reference[0]) {
            std::cerr << "[FAIL] at(0) mismatch at " << where
                      << " step=" << step << " seed=" << seed
                      << " txt=" << text.at(0)
                      << " ref=" << reference[0] << std::endl;
#ifdef BIMODAL_DEBUG
            text.debug_verify_spans(std::cerr);
            text.debug_dump_structure(std::cerr);
#endif
            std::exit(1);
        }
        if (text.at(reference.size() - 1) != reference.back()) {
            std::cerr << "[FAIL] at(last) mismatch at " << where
                      << " step=" << step << " seed=" << seed << std::endl;
#ifdef BIMODAL_DEBUG
            text.debug_verify_spans(std::cerr);
            text.debug_dump_structure(std::cerr);
#endif
            std::exit(1);
        }

        std::mt19937 rng(123456);
        for (int k = 0; k < 10 && reference.size() > 1; ++k) {
            size_t pos = rng() % reference.size();
            char a = text.at(pos);
            char b = reference[pos];
            if (a != b) {
                std::cerr << "[FAIL] at(" << pos << ") mismatch at "
                          << where << " step=" << step << " seed=" << seed
                          << " txt=" << a << " ref=" << b << std::endl;
#ifdef BIMODAL_DEBUG
                text.debug_verify_spans(std::cerr);
                text.debug_dump_structure(std::cerr);
#endif
                std::exit(1);
            }
        }
    }
}

void simple_sanity_tests() {
    BiModalText text;
    std::string reference;

    text.insert(0, "hello");
    reference.insert(0, "hello");
    check_equal(reference, text, "simple/insert-empty", 0, 0);

    text.insert(reference.size(), " world");
    reference.insert(reference.size(), " world");
    check_equal(reference, text, "simple/insert-end", 0, 0);

    text.insert(5, ",");
    reference.insert(5, ",");
    check_equal(reference, text, "simple/insert-mid", 0, 0);

    text.erase(5, 1);
    reference.erase(5, 1);
    check_equal(reference, text, "simple/erase-mid", 0, 0);

    const size_t erase_len = 6;
    text.erase(reference.size() - erase_len, erase_len);
    reference.erase(reference.size() - erase_len, erase_len);
    check_equal(reference, text, "simple/erase-suffix", 0, 0);

    text.clear();
    reference.clear();
    check_equal(reference, text, "simple/clear", 0, 0);

    print_pass("Simple sanity tests");
}

void split_merge_stress_test() {
    BiModalText text;
    std::string reference;

    const size_t big_len = NODE_MAX_SIZE * 3 + 123;
    std::string big(big_len, 'x');

    text.insert(0, big);
    reference.insert(0, big);
    check_equal(reference, text, "split/big-insert", 0, 0);

    text.insert(big_len / 2, "MID");
    reference.insert(big_len / 2, "MID");
    check_equal(reference, text, "split/insert-mid", 0, 0);

    text.insert(0, "HEAD");
    reference.insert(0, "HEAD");
    check_equal(reference, text, "split/insert-head", 0, 0);

    text.insert(reference.size(), "TAIL");
    reference.insert(reference.size(), "TAIL");
    check_equal(reference, text, "split/insert-tail", 0, 0);

    text.erase(10, 1000);
    reference.erase(10, 1000);
    check_equal(reference, text, "split/erase-range1", 0, 0);

    text.erase(reference.size() / 3, NODE_MAX_SIZE / 2);
    reference.erase(reference.size() / 3, NODE_MAX_SIZE / 2);
    check_equal(reference, text, "split/erase-range2", 0, 0);

    text.optimize();
    check_equal(reference, text, "split/optimize", 0, 0);

    print_pass("Split/merge stress test");
}

void random_edit_test(int seed, int ops) {
    BiModalText text;
    std::string reference;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 9);
    std::uniform_int_distribution<int> len_dist(1, 32);
    std::uniform_int_distribution<int> ch_dist(0, 25);

    for (int step = 0; step < ops; ++step) {
        int op = op_dist(rng);

        if (op <= 5) {
            size_t pos = reference.empty() ? 0 : (rng() % (reference.size() + 1));
            int len = len_dist(rng);
            std::string s;
            s.reserve(len);
            for (int i = 0; i < len; ++i) {
                s.push_back(static_cast<char>('a' + ch_dist(rng)));
            }

            text.insert(pos, s);
            reference.insert(pos, s);
            check_equal(reference, text, "random/insert", step, seed);
        } else if (op <= 7) {
            if (!reference.empty()) {
                size_t pos = rng() % reference.size();
                int len = len_dist(rng);
                if (pos + len > reference.size()) len = static_cast<int>(reference.size() - pos);
                if (len > 0) {
                    text.erase(pos, static_cast<size_t>(len));
                    reference.erase(pos, static_cast<size_t>(len));
                    check_equal(reference, text, "random/erase", step, seed);
                }
            }
        } else {
            text.optimize();
            check_equal(reference, text, "random/optimize", step, seed);
        }
    }

    text.optimize();
    check_equal(reference, text, "random/final", ops, seed);

    std::cout << "[PASS] Random edit test seed=" << seed
              << " ops=" << ops << '\n';
}

class InvariantChecker {
public:
    explicit InvariantChecker(const BiModalText& text) : text_(text) {}

    void check_all() const {
        check_size_consistency();
        check_random_access();
        check_iterator_consistency();
    }

private:
    const BiModalText& text_;

    void check_size_consistency() const {
        size_t reported_size = text_.size();
        size_t counted = 0;
        for (auto it = text_.begin(); it != text_.end(); ++it) {
            ++counted;
        }

        if (reported_size != counted) {
            std::cerr << "Invariant violation: size() = " << reported_size
                      << " but iterator counted " << counted << '\n';
            throw std::runtime_error("Size consistency check failed");
        }
    }

    void check_random_access() const {
        if (text_.size() == 0) return;

        size_t idx = 0;
        for (auto it = text_.begin(); it != text_.end(); ++it, ++idx) {
            char via_iterator = *it;
            char via_at = text_.at(idx);

            if (via_iterator != via_at) {
                std::cerr << "Invariant violation at index " << idx
                          << ": iterator='" << via_iterator
                          << "' but at()='" << via_at << "'\n";
                throw std::runtime_error("Random access consistency failed");
            }
        }
    }

    void check_iterator_consistency() const {
        std::string via_to_string = text_.to_string();
        std::string via_iterator;
        for (auto it = text_.begin(); it != text_.end(); ++it) {
            via_iterator += *it;
        }

        if (via_to_string != via_iterator) {
            std::cerr << "Invariant violation: to_string() and iterator output differ\n";
            throw std::runtime_error("Iterator consistency failed");
        }
    }
};

enum class OpType { Insert, Erase, Optimize, Read };

struct Operation {
    OpType type;
    size_t pos;
    std::string data;
    size_t len;
};

class Fuzzer {
public:
    explicit Fuzzer(unsigned seed) : gen_(seed) {}

    void run(int iterations, bool verbose) {
        std::cout << "Running fuzzer with " << iterations
                  << " iterations (seed=" << gen_() << ")...\n";

        for (int i = 0; i < iterations; ++i) {
            OpType op = static_cast<OpType>(op_dist_(gen_));
            try {
                switch (op) {
                    case OpType::Insert:
                        do_insert();
                        break;
                    case OpType::Erase:
                        do_erase();
                        break;
                    case OpType::Optimize:
                        do_optimize();
                        break;
                    case OpType::Read:
                        do_read();
                        break;
                }

                if (i % 100 == 99) {
                    verify_state();
                    if (verbose && i % 500 == 499) {
                        std::cout << "  [" << i + 1 << "/" << iterations << "] "
                                  << "Size: " << text_.size()
                                  << ", Ops: I=" << count_ops(OpType::Insert)
                                  << " E=" << count_ops(OpType::Erase)
                                  << " O=" << count_ops(OpType::Optimize)
                                  << '\n';
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "\n=== Fuzzer caught bug at iteration " << i << " ===\n";
                std::cerr << "Error: " << e.what() << "\n";
                print_recent_history(10);
#ifdef BIMODAL_DEBUG
                try {
                    std::cerr << "\n[DEBUG] Verifying spans & dumping structure...\n";
                    text_.debug_verify_spans(std::cerr);
                    text_.debug_dump_structure(std::cerr);
                } catch (...) {
                    std::cerr << "[DEBUG] Exception during debug verification\n";
                }
#endif
                throw;
            }
        }

        verify_state();
        InvariantChecker(text_).check_all();
        std::cout << "[PASS] Fuzzer completed successfully\n";
        print_stats();
    }

private:
    std::mt19937 gen_;
    BiModalText text_;
    std::string reference_;
    std::vector<Operation> history_;
    std::uniform_int_distribution<> op_dist_{0, 3};

    void do_insert() {
        size_t pos = reference_.empty() ? 0 : gen_() % (reference_.size() + 1);

        int len_choice = gen_() % 100;
        size_t len;
        if (len_choice < 70) {
            len = 1 + gen_() % 10;
        } else if (len_choice < 90) {
            len = 50 + gen_() % 200;
        } else {
            len = 1000 + gen_() % 3000;
        }

        std::string data(len, static_cast<char>('A' + (gen_() % 26)));

        text_.insert(pos, data);
        reference_.insert(pos, data);

        history_.push_back({OpType::Insert, pos, data, 0});
    }

    void do_erase() {
        if (reference_.empty()) return;

        size_t pos = gen_() % reference_.size();
        size_t max_len = reference_.size() - pos;
        size_t len = max_len > 0 ? (1 + gen_() % std::min<size_t>(100, max_len)) : 0;

        if (len > 0) {
            text_.erase(pos, len);
            reference_.erase(pos, len);
            history_.push_back({OpType::Erase, pos, "", len});
        }
    }

    void do_optimize() {
        text_.optimize();
        history_.push_back({OpType::Optimize, 0, "", 0});
    }

    void do_read() {
        if (reference_.empty()) return;

        for (int i = 0; i < 5 && !reference_.empty(); ++i) {
            size_t pos = gen_() % reference_.size();
            char expected = reference_[pos];
            char actual = text_.at(pos);
            if (expected != actual) {
#ifdef BIMODAL_DEBUG
                std::cerr << "[DEBUG] Read mismatch detected in do_read()\n";
                std::cerr << "  pos=" << pos << " expected='" << expected
                          << "' got='" << actual << "'\n";
                try {
                    InvariantChecker(text_).check_all();
                } catch (const std::exception& e) {
                    std::cerr << "[DEBUG] InvariantChecker failed: " << e.what()
                              << "\n";
                }
                try {
                    text_.debug_verify_spans(std::cerr);
                    text_.debug_dump_structure(std::cerr);
                } catch (...) {
                    std::cerr << "[DEBUG] Exception during debug verification\n";
                }
#endif
                throw std::runtime_error(
                    "Read mismatch at pos " + std::to_string(pos) +
                    ": expected '" + expected + "', got '" + actual + "'"
                );
            }
        }
        history_.push_back({OpType::Read, 0, "", 0});
    }

    void verify_state() {
        std::string actual = text_.to_string();
        if (actual != reference_) {
            std::cerr << "STATE DIVERGENCE DETECTED\n"
                      << " Reference size: " << reference_.size() << "\n"
                      << " Actual size: " << actual.size() << "\n";

            size_t diff_pos = 0;
            size_t min_len = std::min(reference_.size(), actual.size());
            while (diff_pos < min_len && reference_[diff_pos] == actual[diff_pos]) {
                ++diff_pos;
            }
            std::cerr << " First diff at: " << diff_pos << "\n";
            if (diff_pos < min_len) {
                std::cerr << "  ref[" << diff_pos << "]='" << reference_[diff_pos]
                          << "'\n";
                std::cerr << "  act[" << diff_pos << "]='" << actual[diff_pos]
                          << "'\n";
            }

            auto slice = [](const std::string& s, size_t pos, size_t radius) {
                if (s.empty()) return std::string{};
                size_t start = (pos > radius) ? pos - radius : 0;
                size_t end = std::min(s.size(), pos + radius + 1);
                return s.substr(start, end - start);
            };
            std::cerr << "  ref context : \"" << slice(reference_, diff_pos, 20)
                      << "\"\n";
            std::cerr << "  act context : \"" << slice(actual, diff_pos, 20)
                      << "\"\n";

            throw std::runtime_error("State verification failed");
        }

        if (text_.size() != reference_.size()) {
            throw std::runtime_error(
                "Size mismatch: bmt.size()=" + std::to_string(text_.size()) +
                " reference.size()=" + std::to_string(reference_.size())
            );
        }
    }

    int count_ops(OpType type) const {
        return static_cast<int>(std::count_if(
            history_.begin(), history_.end(),
            [type](const Operation& op) { return op.type == type; }
        ));
    }

    void print_recent_history(int n) const {
        std::cerr << "\nRecent operations:\n";
        int start = std::max(0, static_cast<int>(history_.size()) - n);
        for (int i = start; i < static_cast<int>(history_.size()); ++i) {
            const auto& op = history_[i];
            std::cerr << "  [" << i << "] ";
            switch (op.type) {
                case OpType::Insert:
                    std::cerr << "INSERT pos=" << op.pos << " len=" << op.data.size();
                    break;
                case OpType::Erase:
                    std::cerr << "ERASE pos=" << op.pos << " len=" << op.len;
                    break;
                case OpType::Optimize:
                    std::cerr << "OPTIMIZE";
                    break;
                case OpType::Read:
                    std::cerr << "READ";
                    break;
            }
            std::cerr << '\n';
        }
    }

    void print_stats() const {
        std::cout << "\nFuzzing statistics:\n"
                  << "  Total operations: " << history_.size() << '\n'
                  << "  Inserts:   " << count_ops(OpType::Insert) << '\n'
                  << "  Erases:    " << count_ops(OpType::Erase) << '\n'
                  << "  Optimizes: " << count_ops(OpType::Optimize) << '\n'
                  << "  Reads:     " << count_ops(OpType::Read) << '\n'
                  << "  Final size: " << reference_.size() << " chars\n";
    }
};

void test_split_boundary() {
    std::cout << "Running split boundary test...\n";
    BiModalText text;

    std::string chunk1(4096, 'A');
    text.insert(0, chunk1);
    assert(text.size() == 4096);

    text.insert(4096, "B");
    assert(text.size() == 4097);
    assert(text.at(4096) == 'B');

    print_pass("Split boundary test");
}

void test_erase_across_nodes() {
    std::cout << "Running cross-node erase test...\n";
    BiModalText text;

    for (int i = 0; i < 3; ++i) {
        std::string chunk(3000, static_cast<char>('A' + i));
        text.insert(text.size(), chunk);
    }

    assert(text.size() == 9000);

    text.erase(2500, 4000);
    assert(text.size() == 5000);

    for (size_t i = 0; i < 2500; ++i) {
        assert(text.at(i) == 'A');
    }

    print_pass("Cross-node erase test");
}

void test_optimize_with_tiny_nodes() {
    std::cout << "Running tiny-node optimize test...\n";
    BiModalText text;

    for (int i = 0; i < 10; ++i) {
        std::string small(100, 'X');
        text.insert(text.size(), small);
        text.optimize();
    }

    assert(text.size() == 1000);
    std::string result = text.to_string();
    assert(result == std::string(1000, 'X'));

    print_pass("Tiny-node optimize test");
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        std::cout << "=== BiModalText unified tests ===\n";

        simple_sanity_tests();
        split_merge_stress_test();
        test_split_boundary();
        test_erase_across_nodes();
        test_optimize_with_tiny_nodes();

        random_edit_test(1, 2000);
        random_edit_test(2, 2000);
        random_edit_test(3, 2000);

        int iterations = (argc > 1) ? std::atoi(argv[1]) : 5000;

        std::cout << "\nStarting fuzzing rounds...\n";
        Fuzzer fuzzer1(42);
        fuzzer1.run(iterations, true);

        Fuzzer fuzzer2(12345);
        fuzzer2.run(iterations / 2, false);

        std::cout << "\n[STRESS] High-intensity fuzzing...\n";
        Fuzzer fuzzer3(99999);
        fuzzer3.run(iterations * 2, true);

        std::cout << "\nAll BiModalText tests passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
