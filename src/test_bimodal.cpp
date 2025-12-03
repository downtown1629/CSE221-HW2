#include <iostream>
#include <random>
#include <cassert>
#include <sstream>
#include "BiModalSkipList.hpp"

using namespace std;

// ANSI 색상 코드
#define GREEN "\033[32m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"

int test_count = 0;
int passed = 0;
int failed = 0;

#define TEST(name) \
    void test_##name(); \
    struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            cout << "\n[TEST " << ++test_count << "] " << #name << "\n"; \
            try { \
                test_##name(); \
                cout << GREEN << "✓ PASSED" << RESET << "\n"; \
                passed++; \
            } catch (const exception& e) { \
                cout << RED << "✗ FAILED: " << e.what() << RESET << "\n"; \
                failed++; \
            } catch (...) { \
                cout << RED << "✗ FAILED: Unknown exception" << RESET << "\n"; \
                failed++; \
            } \
        } \
    } registrar_##name; \
    void test_##name()

// 헬퍼: BiModalText를 std::string으로 변환하여 비교
void verify_content(const BiModalText& bmt, const string& expected, const string& msg = "") {
    string actual = bmt.to_string();
    if (actual != expected) {
        stringstream ss;
        ss << "Content mismatch" << (msg.empty() ? "" : " (" + msg + ")") << "\n"
           << "  Expected: \"" << expected << "\" (len=" << expected.size() << ")\n"
           << "  Actual:   \"" << actual << "\" (len=" << actual.size() << ")";
        throw runtime_error(ss.str());
    }
}

// 헬퍼: 크기 검증
void verify_size(const BiModalText& bmt, size_t expected) {
    if (bmt.size() != expected) {
        stringstream ss;
        ss << "Size mismatch: expected " << expected << ", got " << bmt.size();
        throw runtime_error(ss.str());
    }
}

// ==================== 기본 연산 테스트 ====================

TEST(empty_structure) {
    BiModalText bmt;
    verify_size(bmt, 0);
    verify_content(bmt, "");
}

TEST(single_insert) {
    BiModalText bmt;
    bmt.insert(0, "Hello");
    verify_size(bmt, 5);
    verify_content(bmt, "Hello");
}

TEST(multiple_inserts_at_end) {
    BiModalText bmt;
    bmt.insert(0, "A");
    bmt.insert(1, "B");
    bmt.insert(2, "C");
    verify_content(bmt, "ABC");
}

TEST(insert_at_beginning) {
    BiModalText bmt;
    bmt.insert(0, "World");
    bmt.insert(0, "Hello ");
    verify_content(bmt, "Hello World");
}

TEST(insert_in_middle) {
    BiModalText bmt;
    bmt.insert(0, "AC");
    bmt.insert(1, "B");
    verify_content(bmt, "ABC");
}

TEST(random_access_at) {
    BiModalText bmt;
    bmt.insert(0, "ABCDEFGH");
    assert(bmt.at(0) == 'A');
    assert(bmt.at(3) == 'D');
    assert(bmt.at(7) == 'H');
}

TEST(erase_simple) {
    BiModalText bmt;
    bmt.insert(0, "ABCDE");
    bmt.erase(1, 3); // Remove "BCD"
    verify_content(bmt, "AE");
}

TEST(erase_at_beginning) {
    BiModalText bmt;
    bmt.insert(0, "Hello World");
    bmt.erase(0, 6); // Remove "Hello "
    verify_content(bmt, "World");
}

TEST(erase_at_end) {
    BiModalText bmt;
    bmt.insert(0, "Hello World");
    bmt.erase(5, 6); // Remove " World"
    verify_content(bmt, "Hello");
}

TEST(erase_all) {
    BiModalText bmt;
    bmt.insert(0, "Test");
    bmt.erase(0, 4);
    verify_size(bmt, 0);
    verify_content(bmt, "");
}

// ==================== 노드 분할 테스트 ====================

TEST(node_split_trigger) {
    BiModalText bmt;
    // NODE_MAX_SIZE = 4096을 넘는 데이터 삽입하여 분할 유도
    string large_text(5000, 'X');
    bmt.insert(0, large_text);
    verify_size(bmt, 5000);
    verify_content(bmt, large_text);
}

TEST(multiple_node_splits) {
    BiModalText bmt;
    // 여러 번의 노드 분할 유도
    for (int i = 0; i < 10; ++i) {
        string chunk(1000, 'A' + i);
        bmt.insert(bmt.size(), chunk);
    }
    verify_size(bmt, 10000);

    // 각 청크 검증
    for (int i = 0; i < 10; ++i) {
        assert(bmt.at(i * 1000) == 'A' + i);
    }
}

TEST(split_and_read) {
    BiModalText bmt;
    string data(8000, 'Z'); // 노드 2개로 분할됨
    bmt.insert(0, data);

    // 분할 후에도 모든 위치에서 올바른 값 읽기
    for (size_t i = 0; i < data.size(); i += 100) {
        assert(bmt.at(i) == 'Z');
    }
}

// ==================== 최적화(optimize) 테스트 ====================

TEST(optimize_preserves_content) {
    BiModalText bmt;
    bmt.insert(0, "Hello");
    bmt.insert(5, " World");

    string before = bmt.to_string();
    bmt.optimize();
    string after = bmt.to_string();

    if (before != after) {
        throw runtime_error("optimize() changed content!");
    }
}

TEST(optimize_after_many_edits) {
    BiModalText bmt;
    mt19937 gen(42);
    uniform_int_distribution<> dist(0, 25);

    // 무작위 편집
    for (int i = 0; i < 100; ++i) {
        char c = 'a' + dist(gen);
        size_t pos = bmt.size() > 0 ? gen() % (bmt.size() + 1) : 0;
        bmt.insert(pos, string(1, c));
    }

    string before_opt = bmt.to_string();
    size_t size_before = bmt.size();

    bmt.optimize();

    verify_content(bmt, before_opt, "after optimize");
    verify_size(bmt, size_before);
}

// ==================== 경계 조건 테스트 ====================

TEST(insert_empty_string) {
    BiModalText bmt;
    bmt.insert(0, "");
    verify_size(bmt, 0);
}

TEST(erase_zero_length) {
    BiModalText bmt;
    bmt.insert(0, "ABC");
    bmt.erase(1, 0);
    verify_content(bmt, "ABC");
}

TEST(erase_beyond_end) {
    BiModalText bmt;
    bmt.insert(0, "ABC");
    bmt.erase(1, 1000); // Should only erase to the end
    verify_content(bmt, "A");
}

TEST(at_throws_out_of_range) {
    BiModalText bmt;
    bmt.insert(0, "ABC");

    bool threw = false;
    try {
        bmt.at(10);
    } catch (const out_of_range&) {
        threw = true;
    }

    if (!threw) {
        throw runtime_error("at() should throw out_of_range for invalid index");
    }
}

// ==================== 복잡한 시나리오 ====================

TEST(typing_simulation) {
    BiModalText bmt;
    string reference;

    // 커서가 한 곳에서 계속 타이핑하는 시뮬레이션
    size_t cursor = 0;
    string text = "int main() {\n    return 0;\n}";

    for (char c : text) {
        bmt.insert(cursor, string(1, c));
        reference.insert(cursor, 1, c);
        cursor++;
    }

    verify_content(bmt, reference);
}

TEST(backspace_simulation) {
    BiModalText bmt;
    bmt.insert(0, "Hello World");

    // "World" 삭제 (백스페이스 5번)
    for (int i = 0; i < 5; ++i) {
        bmt.erase(bmt.size() - 1, 1);
    }

    verify_content(bmt, "Hello ");
}

TEST(refactoring_simulation) {
    BiModalText bmt;
    bmt.insert(0, "function oldName() {}");

    // "oldName"을 "newName"으로 변경
    bmt.erase(9, 7); // Remove "oldName"
    bmt.insert(9, "newName");

    verify_content(bmt, "function newName() {}");
}

// ==================== Iterator 테스트 ====================

TEST(iterator_full_scan) {
    BiModalText bmt;
    string text = "ABCDEFGHIJ";
    bmt.insert(0, text);

    string scanned;
    for (auto it = bmt.begin(); it != bmt.end(); ++it) {
        scanned += *it;
    }

    if (scanned != text) {
        throw runtime_error("Iterator scan mismatch");
    }
}

TEST(scan_method) {
    BiModalText bmt;
    bmt.insert(0, "12345");

    string result;
    bmt.scan([&](char c) {
        result += c;
    });

    verify_content(bmt, result);
}

// ==================== 랜덤 Fuzzing 스타일 테스트 ====================

TEST(random_operations_small) {
    BiModalText bmt;
    string reference;

    mt19937 gen(12345);
    uniform_int_distribution<> op_dist(0, 2); // 0: insert, 1: erase, 2: read

    for (int i = 0; i < 200; ++i) {
        int op = op_dist(gen);

        if (op == 0) { // Insert
            size_t pos = reference.empty() ? 0 : gen() % (reference.size() + 1);
            char c = 'A' + (gen() % 26);

            bmt.insert(pos, string(1, c));
            reference.insert(pos, 1, c);

        } else if (op == 1 && !reference.empty()) { // Erase
            size_t pos = gen() % reference.size();
            size_t len = 1 + gen() % min<size_t>(10, reference.size() - pos);

            bmt.erase(pos, len);
            reference.erase(pos, len);
        } else if (!reference.empty()) { // Read
            size_t pos = gen() % reference.size();
            if (bmt.at(pos) != reference[pos]) {
                stringstream ss;
                ss << "Random test: at(" << pos << ") mismatch";
                throw runtime_error(ss.str());
            }
        }

        // 주기적으로 전체 내용 검증
        if (i % 50 == 49) {
            verify_content(bmt, reference, "random ops iteration " + to_string(i));
        }
    }

    verify_content(bmt, reference, "final");
}

TEST(random_operations_with_optimize) {
    BiModalText bmt;
    string reference;

    mt19937 gen(99999);

    for (int i = 0; i < 300; ++i) {
        // 삽입
        if (gen() % 3 == 0) {
            size_t pos = reference.empty() ? 0 : gen() % (reference.size() + 1);
            string chunk(1 + gen() % 20, 'X');

            bmt.insert(pos, chunk);
            reference.insert(pos, chunk);
        }

        // 삭제
        if (gen() % 5 == 0 && !reference.empty()) {
            size_t pos = gen() % reference.size();
            size_t len = 1 + gen() % min<size_t>(30, reference.size() - pos);

            bmt.erase(pos, len);
            reference.erase(pos, len);
        }

        // 최적화
        if (i % 100 == 99) {
            bmt.optimize();
            verify_content(bmt, reference, "after optimize in random test");
        }
    }

    verify_content(bmt, reference, "final with optimize");
}

// ==================== 대용량 데이터 테스트 ====================

TEST(large_document) {
    BiModalText bmt;

    // 100KB 문서 생성
    const size_t DOC_SIZE = 100000;
    string document;
    document.reserve(DOC_SIZE);

    for (size_t i = 0; i < DOC_SIZE; ++i) {
        document += static_cast<char>('A' + (i % 26));
    }

    // 한 번에 삽입
    bmt.insert(0, document);
    verify_size(bmt, DOC_SIZE);

    // 샘플링 검증 (전체 검증은 너무 느림)
    mt19937 gen(777);
    for (int i = 0; i < 1000; ++i) {
        size_t pos = gen() % DOC_SIZE;
        assert(bmt.at(pos) == document[pos]);
    }

    cout << "  (Verified 100KB document with sampling)\n";
}

TEST(stress_test_splits_and_merges) {
    BiModalText bmt;

    // 많은 노드 생성 유도
    for (int i = 0; i < 20; ++i) {
        string chunk(3000, 'A' + (i % 26));
        bmt.insert(bmt.size(), chunk);
    }

    verify_size(bmt, 60000);

    // 최적화로 병합 유도
    bmt.optimize();
    verify_size(bmt, 60000);

    // 중간에서 대량 삭제
    bmt.erase(10000, 40000);
    verify_size(bmt, 20000);

    cout << "  (Created/merged/deleted across 20 chunks)\n";
}

// ==================== Main ====================

int main() {
    cout << "\n";
    cout << "╔════════════════════════════════════════════════╗\n";
    cout << "║   BiModalText Validation Test Suite         ║\n";
    cout << "╚════════════════════════════════════════════════╝\n";

    // 테스트는 static 객체 초기화 시점에 자동 실행됨

    cout << "\n";
    cout << "════════════════════════════════════════════════\n";
    cout << "  Total:  " << test_count << " tests\n";
    cout << GREEN << "  Passed: " << passed << RESET << "\n";

    if (failed > 0) {
        cout << RED << "  Failed: " << failed << RESET << "\n";
        cout << "════════════════════════════════════════════════\n";
        return 1;
    } else {
        cout << "  Failed: 0\n";
        cout << "════════════════════════════════════════════════\n";
        cout << GREEN << "\n✓ All tests passed!\n" << RESET << "\n";
        return 0;
    }
}
