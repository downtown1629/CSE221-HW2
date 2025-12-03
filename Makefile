CXXFLAGS   = -std=c++17 -O3 -march=native -Wall -Wextra -Isrc
DEBUGFLAGS = -std=c++17 -Og -march=native -g -fsanitize=address -Wall -Wextra -Isrc

TARGET = main

SRC_DIR  = src
SOURCES  = $(SRC_DIR)/benchmark.cpp

# 항상 다시 빌드되도록
.PHONY: $(TARGET) all run clean debug

all: $(TARGET)

$(TARGET):
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

# debug 빌드
debug: CXXFLAGS=$(DEBUGFLAGS)
debug: clean $(TARGET)
