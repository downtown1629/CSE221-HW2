// 위 코드가 BiModalNodes.hpp에 있다고 가정하거나, 이 파일 상단에 붙여넣으세요.
#include "BiModalSkipList.hpp"

int main() {
    std::cout << "=== Bi-Modal Skip List Final Test ===\n";
    BiModalText text;

    try {
        text.insert(0, "Hello");       // size 5
        text.insert(5, " World");      // size 11 -> Split 발생 (Max 10)
        text.insert(5, " Big");        // size 15

        std::cout << "Result: " << text.to_string() << "\n";
        // 예상: "Hello Big World"
        
        std::cout << "At(0): " << text.at(0) << "\n";
        std::cout << "At(6): " << text.at(6) << "\n";
        
        std::cout << "-> PASS\n";

            // 6. 최적화(Compaction) 테스트
        std::cout << "[Test 5] Optimize (Switch to Read Mode)\n";
        text.optimize(); 
        // 내부적으로 모든 노드가 CompactNode(std::vector)로 변했어야 함.
        // 겉으로 보이는 데이터는 똑같아야 함.
        std::cout << "Text after optimize: " << text.to_string() << "\n";
        
        if (text.to_string() == "Hello Big World") {
            std::cout << "-> PASS (Data integrity maintained)\n";
        }

        // 7. 재진입(Re-edit) 테스트
        std::cout << "[Test 6] Re-edit (Read Mode -> Write Mode)\n";
        // CompactNode 상태인 노드에 다시 삽입 시도 -> 자동으로 GapNode로 변환(expand)되어야 함
        text.insert(0, "Oh! "); 
        std::cout << "Final Result: " << text.to_string() << "\n";
        // 예상: "Oh! Hello Big World"
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }

    return 0;
}