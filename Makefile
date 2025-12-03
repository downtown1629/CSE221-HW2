CXX = g++

CXXFLAGS = -std=c++17 -O2 -march=native -Wall -Wextra -Isrc
DEBUGFLAGS = -std=c++17 -Og -march=native -g -fsanitize=address -Wall -Wextra -Isrc

TARGET = main


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

debug: $(TARGET)
	$(TARGET): $(SOURCES)
	$(CXX) $(DEBUGFLAGS) -o $(TARGET) $(SOURCES)