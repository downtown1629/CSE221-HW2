#include <iostream>
#include <random>
#include <vector>
#include <string>
#include <cassert>
#include "BiModalSkipList.hpp"

using namespace std;

// ==================== Invariant Checker ====================

class InvariantChecker {
    const BiModalText& bmt;

public:
    explicit InvariantChecker(const BiModalText& b) : bmt(b) {}

    // 내부 구조를 직접 검증할 수는 없지만, 외부 동작으로 간접 검증
    void check_all() {
        check_size_consistency();
        check_random_access();
        check_iterator_consistency();
    }

    void check_size_consistency() {
        size_t reported_size = bmt.size();

        // Iterator로 센 크기와 일치해야 함
        size_t counted = 0;
        for (auto it = bmt.begin(); it != bmt.end(); ++it) {
            counted++;
        }

        if (reported_size != counted) {
            cerr << "INVARIANT VIOLATION: size() = " << reported_size
                 << " but iterator counted " << counted << " elements\n";
            throw runtime_error("Size consistency check failed");
        }
    }

    void check_random_access() {
        if (bmt.size() == 0) return;

        // at()과 iterator 값이 일치해야 함
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
        // to_string()과 iterator가 같은 결과를 내야 함
        string via_to_string = bmt.to_string();

        string via_iterator;
        for (auto it = bmt.begin(); it != bmt.end(); ++it) {
            via_iterator += *it;
        }

        if (via_to_string != via_iterator) {
            cerr << "INVARIANT VIOLATION:\n"
                 << "  to_string(): \"" << via_to_string << "\"\n"
                 << "  iterator:    \"" << via_iterator << "\"\n";
            throw runtime_error("Iterator consistency failed");
        }
    }
};

// ==================== Fuzzing Operations ====================

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
    string reference; // Ground truth
    vector<Operation> history;

    uniform_int_distribution<> op_dist{0, 3};

public:
    Fuzzer(unsigned seed) : gen(seed) {}

    void run(int iterations, bool verbose = false) {
        cout << "Running fuzzer with " << iterations << " iterations (seed=" << gen() << ")...\n";

        for (int i = 0; i < iterations; ++i) {
            OpType op = static_cast<OpType>(op_dist(gen));

            try {
                switch (op) {
                    case OP_INSERT:
                        do_insert();
                        break;
                    case OP_ERASE:
                        do_erase();
                        break;
                    case OP_OPTIMIZE:
                        do_optimize();
                        break;
                    case OP_READ:
                        do_read();
                        break;
                }

                // 주기적으로 불변식 검증
                if (i % 100 == 99) {
                    verify_state();
                    if (verbose && i % 500 == 499) {
                        cout << "  [" << i+1 << "/" << iterations << "] "
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
                throw;
            }
        }

        // 최종 검증
        verify_state();
        InvariantChecker(bmt).check_all();

        cout << "✓ Fuzzer completed successfully\n";
        print_stats();
    }

private:
    void do_insert() {
        size_t pos = reference.empty() ? 0 : gen() % (reference.size() + 1);

        // 다양한 길이의 문자열 삽입
        int len_choice = gen() % 100;
        size_t len;
        if (len_choice < 70) {
            len = 1 + gen() % 10; // 짧은 문자열 (70%)
        } else if (len_choice < 90) {
            len = 50 + gen() % 200; // 중간 (20%)
        } else {
            len = 1000 + gen() % 3000; // 긴 문자열로 노드 분할 유도 (10%)
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

        // 여러 위치에서 랜덤 읽기
        for (int i = 0; i < 5 && !reference.empty(); ++i) {
            size_t pos = gen() % reference.size();
            char expected = reference[pos];
            char actual = bmt.at(pos);

            if (expected != actual) {
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
                 << "  Reference size: " << reference.size() << "\n"
                 << "  Actual size:    " << bmt.size() << "\n";

            // 차이나는 첫 위치 찾기
            size_t diff_pos = 0;
            size_t min_len = min(reference.size(), actual.size());
            while (diff_pos < min_len && reference[diff_pos] == actual[diff_pos]) {
                diff_pos++;
            }

            cerr << "  First diff at:  " << diff_pos << "\n";
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

// ==================== Specific Bug Hunting Tests ====================

void test_split_boundary() {
    cout << "\n[BOUNDARY TEST] Node split at exact NODE_MAX_SIZE...\n";
    BiModalText bmt;

    // 정확히 4096 바이트 삽입 (분할 안 됨)
    string chunk1(4096, 'A');
    bmt.insert(0, chunk1);
    assert(bmt.size() == 4096);

    // 1바이트 더 추가 -> 분할 트리거
    bmt.insert(4096, "B");
    assert(bmt.size() == 4097);
    assert(bmt.at(4096) == 'B');

    cout << "✓ Split boundary test passed\n";
}

void test_erase_across_nodes() {
    cout << "\n[CROSS-NODE TEST] Erase spanning multiple nodes...\n";
    BiModalText bmt;

    // 3개 노드 생성
    for (int i = 0; i < 3; ++i) {
        string chunk(3000, 'A' + i);
        bmt.insert(bmt.size(), chunk);
    }

    assert(bmt.size() == 9000);

    // 중간 노드를 걸쳐서 삭제 (pos=2500, len=4000)
    bmt.erase(2500, 4000);
    assert(bmt.size() == 5000);

    // 남은 부분이 올바른지 확인
    for (size_t i = 0; i < 2500; ++i) {
        assert(bmt.at(i) == 'A');
    }

    cout << "✓ Cross-node erase test passed\n";
}

void test_optimize_with_tiny_nodes() {
    cout << "\n[MERGE TEST] Optimize merging small nodes...\n";
    BiModalText bmt;

    // 작은 노드 여러 개 생성 후 optimize로 병합 유도
    for (int i = 0; i < 10; ++i) {
        string small(100, 'X');
        bmt.insert(bmt.size(), small);
        bmt.optimize(); // 매번 최적화 -> 병합 발생 가능
    }

    assert(bmt.size() == 1000);

    string result = bmt.to_string();
    assert(result == string(1000, 'X'));

    cout << "✓ Tiny node merge test passed\n";
}

// ==================== Main ====================

int main(int argc, char* argv[]) {
    cout << "\n";
    cout << "╔═══════════════════════════════════════════════════╗\n";
    cout << "║   BiModalText Advanced Fuzzing & Verification   ║\n";
    cout << "╚═══════════════════════════════════════════════════╝\n";

    try {
        // Boundary tests
        test_split_boundary();
        test_erase_across_nodes();
        test_optimize_with_tiny_nodes();

        // Fuzzing rounds
        int iterations = (argc > 1) ? atoi(argv[1]) : 5000;

        cout << "\n" << string(50, '=') << "\n";
        cout << "Starting fuzzing rounds...\n";
        cout << string(50, '=') << "\n";

        // Round 1: Balanced operations
        Fuzzer fuzzer1(42);
        fuzzer1.run(iterations, true);

        // Round 2: Different seed
        Fuzzer fuzzer2(12345);
        fuzzer2.run(iterations / 2, false);

        // Round 3: Stress test
        cout << "\n[STRESS] High-intensity fuzzing...\n";
        Fuzzer fuzzer3(99999);
        fuzzer3.run(iterations * 2, true);

        cout << "\n" << string(50, '=') << "\n";
        cout << "\033[32m✓ ALL FUZZING TESTS PASSED\033[0m\n";
        cout << string(50, '=') << "\n\n";

        return 0;

    } catch (const exception& e) {
        cerr << "\n\033[31m✗ FUZZING FAILED: " << e.what() << "\033[0m\n\n";
        return 1;
    }
}
