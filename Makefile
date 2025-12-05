MAIN_FLAGS   = -std=c++20 -O3 -march=native -Wall -Wextra -Isrc
FUZZER_FLAGS = -std=c++20 -Og -march=native -g -fsanitize=address -Wall -Wextra -Isrc -DBIMODAL_DEBUG=ON

SRC_DIR   = src
MAIN_SRC  = $(SRC_DIR)/benchmark.cpp
FUZZ_SRC  = $(SRC_DIR)/fuzzer.cpp
HEAVY_SRC = $(SRC_DIR)/bench_heavy.cpp

TARGET = main
FUZZER = fuzzer
HEAVY  = heavy

.PHONY: all run clean debug

all: $(TARGET) $(FUZZER) $(HEAVY)

$(TARGET): $(MAIN_SRC)
	$(CXX) $(MAIN_FLAGS) -o $@ $<

$(FUZZER): $(FUZZ_SRC)
	$(CXX) $(FUZZER_FLAGS) -o $@ $<

$(HEAVY): $(HEAVY_SRC)
	$(CXX) $(MAIN_FLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(FUZZER) $(HEAVY)

debug: $(FUZZER)
