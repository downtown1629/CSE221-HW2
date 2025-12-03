#include <iostream>
#include <random>
#include <string>
#include <stdexcept>
#include <cassert>

#include "BiModalSkipList.hpp"  // benchmark.cpp에서 쓰던 그 헤더

using std::cout;
using std::endl;

// 공통 검증 함수: std::string(ref) vs BiModalText(txt)
void check_equal(const std::string& ref, const BiModalText& txt,
                 const char* where, int step, int seed) {
    if (ref.size() != txt.size()) {
        std::cerr << "[FAIL] size mismatch at " << where
                  << " step=" << step << " seed=" << seed
                  << " ref=" << ref.size()
                  << " txt=" << txt.size() << std::endl;
        std::exit(1);
    }

    // to_string() 비교
    std::string txt_str = txt.to_string();
    if (txt_str != ref) {
        std::cerr << "[FAIL] content mismatch at " << where
                  << " step=" << step << " seed=" << seed << std::endl;
        std::cerr << "ref : \"" << ref << "\"" << std::endl;
        std::cerr << "txt : \"" << txt_str << "\"" << std::endl;
        std::exit(1);
    }

    // iterator(begin/end)로 한 번 더 확인
    std::string via_iter;
    via_iter.reserve(ref.size());
    for (char c : txt) {
        via_iter.push_back(c);
    }
    if (via_iter != ref) {
        std::cerr << "[FAIL] iterator mismatch at " << where
                  << " step=" << step << " seed=" << seed << std::endl;
        std::exit(1);
    }

    // spot-check at() 몇 개
    if (!ref.empty()) {
        // first / last
        if (txt.at(0) != ref[0]) {
            std::cerr << "[FAIL] at(0) mismatch at " << where << std::endl;
            std::exit(1);
        }
        if (txt.at(ref.size() - 1) != ref.back()) {
            std::cerr << "[FAIL] at(last) mismatch at " << where << std::endl;
            std::exit(1);
        }
        // 랜덤 인덱스 몇 개
        std::mt19937 rng(123456); // deterministic
        for (int k = 0; k < 10 && ref.size() > 1; ++k) {
            size_t pos = rng() % ref.size();
            if (txt.at(pos) != ref[pos]) {
                std::cerr << "[FAIL] at(" << pos << ") mismatch at " << where
                          << " step=" << step << " seed=" << seed << std::endl;
                std::exit(1);
            }
        }
    }
}

// 1) 기본적인 동작 확인용
void simple_sanity_tests() {
    BiModalText txt;
    std::string ref;

    // insert into empty
    txt.insert(0, "hello");
    ref.insert(0, "hello");
    check_equal(ref, txt, "simple/insert-empty", 0, 0);

    // insert at end
    txt.insert(ref.size(), " world");
    ref.insert(ref.size(), " world");
    check_equal(ref, txt, "simple/insert-end", 0, 0);

    // insert in middle
    txt.insert(5, ",");
    ref.insert(5, ",");
    check_equal(ref, txt, "simple/insert-mid", 0, 0);

    // erase from middle
    txt.erase(5, 1);
    ref.erase(5, 1);
    check_equal(ref, txt, "simple/erase-mid", 0, 0);

    // erase suffix
    size_t erase_len = 6;
    txt.erase(ref.size() - erase_len, erase_len);
    ref.erase(ref.size() - erase_len, erase_len);
    check_equal(ref, txt, "simple/erase-suffix", 0, 0);

    // clear
    txt.clear();
    ref.clear();
    check_equal(ref, txt, "simple/clear", 0, 0);

    std::cout << "[OK] simple sanity tests passed" << std::endl;
}

// 2) NODE_MAX_SIZE를 넘긴 큰 텍스트로 split/merge 스트레스
void split_merge_stress_test() {
    BiModalText txt;
    std::string ref;

    // NODE_MAX_SIZE는 Nodes.hpp에 있는 전역 상수
    const size_t big_len = NODE_MAX_SIZE * 3 + 123;
    std::string big(big_len, 'x');

    // 큰 블록 한 번에 넣어서 노드 여러 개 만들기
    txt.insert(0, big);
    ref.insert(0, big);
    check_equal(ref, txt, "split/big-insert", 0, 0);

    // 중간 insert (노드 안쪽 / 경계 근처)
    txt.insert(big_len / 2, "MID");
    ref.insert(big_len / 2, "MID");
    check_equal(ref, txt, "split/insert-mid", 0, 0);

    // 앞/뒤 insert
    txt.insert(0, "HEAD");
    ref.insert(0, "HEAD");
    check_equal(ref, txt, "split/insert-head", 0, 0);

    txt.insert(ref.size(), "TAIL");
    ref.insert(ref.size(), "TAIL");
    check_equal(ref, txt, "split/insert-tail", 0, 0);

    // 여러 노드에 걸쳐 있을 법한 범위 삭제
    txt.erase(10, 1000);
    ref.erase(10, 1000);
    check_equal(ref, txt, "split/erase-range1", 0, 0);

    txt.erase(ref.size() / 3, NODE_MAX_SIZE / 2);
    ref.erase(ref.size() / 3, NODE_MAX_SIZE / 2);
    check_equal(ref, txt, "split/erase-range2", 0, 0);

    // optimize() 후에도 동일한지 확인 (GapNode -> CompactNode 변환 + 병합)
    txt.optimize();
    check_equal(ref, txt, "split/optimize", 0, 0);

    std::cout << "[OK] split/merge stress test passed" << std::endl;
}

// 3) 랜덤 편집 + 중간 중간 optimize()
void random_edit_test(int seed, int ops) {
    BiModalText txt;
    std::string ref;

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> op_dist(0, 9); // 0-5: insert, 6-7: erase, 8-9: optimize
    std::uniform_int_distribution<int> len_dist(1, 32);
    std::uniform_int_distribution<int> ch_dist(0, 25);

    for (int step = 0; step < ops; ++step) {
        int op = op_dist(rng);

        if (op <= 5) {
            // insert 랜덤 문자열
            size_t pos = ref.empty() ? 0 : (rng() % (ref.size() + 1));
            int len = len_dist(rng);
            std::string s;
            s.reserve(len);
            for (int i = 0; i < len; ++i) {
                s.push_back(static_cast<char>('a' + ch_dist(rng)));
            }

            txt.insert(pos, s);
            ref.insert(pos, s);
            check_equal(ref, txt, "random/insert", step, seed);
        } else if (op <= 7) {
            // erase 랜덤 범위
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
            // optimize 호출
            txt.optimize();
            check_equal(ref, txt, "random/optimize", step, seed);
        }
    }

    // 마지막으로 한 번 더 optimize
    txt.optimize();
    check_equal(ref, txt, "random/final", ops, seed);

    std::cout << "[OK] random test seed=" << seed
              << " ops=" << ops << " passed" << std::endl;
}

int main() {
    try {
        simple_sanity_tests();
        split_merge_stress_test();

        // 여러 seed로 랜덤 테스트 (ops는 적당히 늘려도 됨)
        random_edit_test(1, 2000);
        random_edit_test(2, 2000);
        random_edit_test(3, 2000);

        std::cout << "All BiModalText tests passed." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}   
