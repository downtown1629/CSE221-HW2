# 컴파일러 설정
CXX = g++

# 컴파일 플래그
# -std=c++17: 과제 권장 사양 (C++17) [cite: 69]
# -O3: 벤치마크 성능을 극대화하기 위한 최적화 옵션 (매우 중요!)
# -Wall -Wextra: 잠재적인 오류나 경고를 확인하기 위한 옵션
# -Isrc: src 폴더 내의 헤더 파일을 찾을 수 있도록 인클루드 경로 추가
CXXFLAGS = -std=c++17 -O3 -march=native -Wall -Wextra -Isrc

# 실행 파일 이름
TARGET = main

# 소스 파일 및 경로 정의
SRC_DIR = src
SOURCES = $(SRC_DIR)/benchmark.cpp

# 기본 타겟 (make 입력 시 실행) [cite: 11]
all: $(TARGET)

# 실행 파일 빌드 규칙
# benchmark.cpp를 컴파일하여 main 실행 파일을 생성합니다.
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

# 실행 규칙 (make run 입력 시 실행) 
run: $(TARGET)
	./$(TARGET)

# 청소 규칙 (make clean)
# 빌드된 실행 파일을 삭제합니다.
clean:
	rm -f $(TARGET)

# 가짜 타겟 정의 (파일 이름과 겹치지 않도록 방지)
.PHONY: all run clean