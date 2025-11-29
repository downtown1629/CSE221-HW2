#include <iostream>
#include <vector>
#include <list>
#include <chrono>
#include <numeric>
#include <string>
#include <iomanip>

// 앞서 만든 BiModalText 클래스를 포함하세요.
// 파일로 분리했다면: #include "BiModalSkipList.hpp"
// 아니면 이 코드 상단에 클래스 전체를 복사해 넣으세요.
#include "BiModalSkipList.hpp" 

using namespace std;
using namespace std::chrono;

// --- 타이머 유틸리티 ---
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

// --- 테스트 파라미터 ---
const int INITIAL_SIZE = 1000000;   // 초기 텍스트 크기
const int INSERT_COUNT = 100000;    // 중간 삽입 횟수 (Typing Simulation)
const string CHUNK = "A";         // 한 번에 타이핑하는 문자열

// 1. std::vector 벤치마크
void bench_vector() {
    vector<char> v(INITIAL_SIZE, 'x');
    long long checksum = 0;
    Timer t;

    // A. Typing Phase (중간 삽입)
    // 벡터는 중간 삽입 시 뒤쪽 데이터를 모두 밀어야 하므로 O(N) 발생
    size_t mid = v.size() / 2;
    t.reset();
    for (int i = 0; i < INSERT_COUNT; ++i) {
        v.insert(v.begin() + mid, 'A');
    }
    double time_insert = t.elapsed_ms();

    // B. Reading Phase (전체 순회)
    t.reset();
    for (char c : v) checksum += c;
    double time_read = t.elapsed_ms();

    cout << left << setw(15) << "std::vector" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Slow Insert, Fast Read)" << endl;
}

// 2. std::list 벤치마크
void bench_list() {
    list<char> l(INITIAL_SIZE, 'x');
    long long checksum = 0;
    Timer t;

    // A. Typing Phase
    // 리스트는 삽입 자체는 O(1)이나, 해당 위치까지 가는 iterator 이동이 O(N)
    t.reset();
    auto it = l.begin();
    advance(it, l.size() / 2); // 초기 중앙 위치
    
    for (int i = 0; i < INSERT_COUNT; ++i) {
        l.insert(it, 'A');
        // 연속 타이핑 시뮬레이션: 커서는 방금 쓴 글자 뒤에 위치한다고 가정 (iterator 유지)
        // 만약 매번 인덱스 0부터 찾는다면 훨씬 느리겠지만, 여기서는 최대한 유리하게 측정
    }
    double time_insert = t.elapsed_ms();

    // B. Reading Phase
    t.reset();
    for (char c : l) checksum += c;
    double time_read = t.elapsed_ms();

    cout << left << setw(15) << "std::list" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Slow Search/Read, Cache Miss)" << endl;
}

// 3. BiModalText 벤치마크 (제안하는 자료구조)
void bench_bimodal() {
    BiModalText bmt;
    // 초기 데이터 채우기 (Append는 빠름)
    string init_str(1000, 'x'); // 1000자씩 50번
    for(int i=0; i<INITIAL_SIZE/1000; ++i) bmt.insert(bmt.size(), init_str);
    
    // 강제로 Compact 모드로 시작 (공정한 비교를 위해)
    bmt.optimize(); 


    // A. Typing Phase
    // Skip List 탐색(O(log N)) + Gap Buffer 삽입(O(1))
    Timer t;
    size_t mid = bmt.size() / 2;
    for (int i = 0; i < INSERT_COUNT; ++i) {
        bmt.insert(mid + i, "A"); 
    }
    double time_insert = t.elapsed_ms();

    // 최적화 수행 (Read Mode 전환)
    Timer t_opt;
    bmt.optimize();
    double time_opt = t_opt.elapsed_ms();

    // B. Reading Phase
    t.reset();
    long long checksum = 0;

    // at()을 이용한 순회는 O(N log N)이라 느릴 수 있음.
    // 하지만 내부 노드가 CompactNode라면 데이터 지역성이 좋음.
    // *실제 구현에서는 Iterator가 있어야 O(N)이지만, 여기서는 at()으로 측정
    // (만약 너무 느리면 iterator 구현이 필요하지만, 과제 범위상 at() 성능 비교로 충분)
    for (auto it = bmt.begin(); it != bmt.end(); ++it) {
        checksum += *it;
    }
    double time_read = t.elapsed_ms();

    cout << left << setw(15) << "BiModalText" 
         << setw(15) << time_insert 
         << setw(15) << time_read 
         << " (Fast Search+Insert, Good Read)" << endl;
    
    cout << "   * Optimization Overhead: " << time_opt << " ms" << endl;
}

// [새로운 벤치마크] 랜덤 인덱스 삽입 (진짜 성능 비교)
void bench_random_access_insertion() {
    // 테스트 크기 조정 (List가 너무 느려서 100만 개는 불가능할 수 있음)
    const int TEST_SIZE = 100000; 
    const int RAND_INSERTS = 10000;
    
    std::cout << "\n[Scenario: Random Index Insertion (N=" << TEST_SIZE 
              << ", Inserts=" << RAND_INSERTS << ")]\n";
    std::cout << "--------------------------------------------------------------\n";
    std::cout << std::left << std::setw(15) << "Structure" 
              << std::setw(15) << "Time (ms)" 
              << "Note" << std::endl;
    std::cout << "--------------------------------------------------------------\n";

    // 랜덤 엔진
    std::mt19937 gen(1234); // 고정 시드

    // 1. std::vector
    {
        std::vector<char> v(TEST_SIZE, 'x');
        std::uniform_int_distribution<> dist(0, TEST_SIZE);
        
        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % v.size();
            // 위치 계산은 O(1)이지만, 데이터 이동이 O(N)
            v.insert(v.begin() + pos, 'A'); 
        }
        std::cout << std::left << std::setw(15) << "std::vector" 
                  << std::setw(15) << t.elapsed_ms() 
                  << "(Fast Seek, Slow Shift)" << std::endl;
    }

    // 2. std::list
    {
        std::list<char> l(TEST_SIZE, 'x');
        std::uniform_int_distribution<> dist(0, TEST_SIZE);

        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % l.size();
            
            // [핵심] 리스트는 인덱스로 바로 못 감 -> 처음부터 걸어가야 함 O(N)
            auto it = l.begin();
            std::advance(it, pos); 
            
            // 삽입 자체는 O(1)
            l.insert(it, 'A');
        }
        std::cout << std::left << std::setw(15) << "std::list" 
                  << std::setw(15) << t.elapsed_ms() 
                  << "(Slow Seek, Fast Insert)" << std::endl;
    }

    // 3. BiModalText
    {
        BiModalText bmt;
        std::string chunk(100, 'x');
        for(int i=0; i<TEST_SIZE/100; ++i) bmt.insert(0, chunk);
        bmt.optimize(); // 공정한 비교를 위해 정리 후 시작

        std::uniform_int_distribution<> dist(0, TEST_SIZE);

        Timer t;
        for (int i = 0; i < RAND_INSERTS; ++i) {
            size_t pos = dist(gen) % bmt.size();
            // 검색 O(log N) + 삽입 O(1)
            bmt.insert(pos, "A"); 
        }
        std::cout << std::left << std::setw(15) << "BiModalText" 
                  << std::setw(15) << t.elapsed_ms() 
                  << "(Log Seek, Fast Insert)" << std::endl;
    }
    std::cout << "--------------------------------------------------------------\n";
}

int main() {
    cout << "==============================================================" << endl;
    cout << " Benchmark: N=" << INITIAL_SIZE << ", Inserts=" << INSERT_COUNT << endl;
    cout << "==============================================================" << endl;
    cout << left << setw(15) << "Structure" 
         << setw(15) << "Insert (ms)" 
         << setw(15) << "Read (ms)" 
         << "Note" << endl;
    cout << "--------------------------------------------------------------" << endl;

    bench_vector();
    bench_list();
    bench_bimodal();
    bench_random_access_insertion();

    cout << "==============================================================" << endl;
    return 0;
}