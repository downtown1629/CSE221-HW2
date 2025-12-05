MAIN_FLAGS   = -std=c++20 -O3 -march=native -Wall -Wextra -Isrc
FUZZER_FLAGS = -std=c++20 -Og -march=native -g -fsanitize=address -Wall -Wextra -Isrc

SRC_DIR   = src
MAIN_SRC  = $(SRC_DIR)/benchmark.cpp
FUZZ_SRC  = $(SRC_DIR)/fuzzer.cpp

TARGET = main
FUZZER = fuzzer

.PHONY: all run clean debug

all: $(TARGET) $(FUZZER)

$(TARGET): $(MAIN_SRC)
	$(CXX) $(MAIN_FLAGS) -o $@ $<

$(FUZZER): $(FUZZ_SRC)
	$(CXX) $(FUZZER_FLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(FUZZER)

debug: clean $(FUZZER)
