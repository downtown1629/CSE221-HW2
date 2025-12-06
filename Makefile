MAIN_FLAGS   = -std=c++20 -O3 -march=native -Wall -Wextra -Isrc
FUZZER_FLAGS = -std=c++20 -Og -march=native -g -fsanitize=address -Wall -Wextra -Isrc -DBIMODAL_DEBUG=ON

SRC_DIR   = src
MAIN_SRC  = $(SRC_DIR)/benchmark.cpp
FUZZ_SRC  = $(SRC_DIR)/fuzzer.cpp
LIBROPE_C = librope/rope.c
LIBROPE_WRAPPER = $(SRC_DIR)/librope_wrapper.cpp

TARGET = main
FUZZER = fuzzer
LIBROPE_O = librope/rope.o

.PHONY: all run clean debug librope

all: $(TARGET) $(FUZZER)

$(TARGET): $(MAIN_SRC) $(LIBROPE_O)
	$(CXX) $(MAIN_FLAGS) -DLIBROPE=ON $(LIBROPE_O) -o $@ $<

librope: $(MAIN_SRC) $(LIBROPE_O)
	$(CXX) $(MAIN_FLAGS) -DLIBROPE=ON $(LIBROPE_O) -o $(TARGET) $<

$(FUZZER): $(FUZZ_SRC)
	$(CXX) $(FUZZER_FLAGS) -o $@ $<

$(LIBROPE_O): $(LIBROPE_C) librope/rope.h
	$(CC) -std=c99 -O3 -march=native -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(FUZZER) $(LIBROPE_O)

debug: $(FUZZER)
