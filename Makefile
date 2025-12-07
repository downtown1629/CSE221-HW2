# Compiler flags
MAIN_FLAGS   = -std=c++20 -O3 -march=native -Wall -Wextra -Isrc
FUZZER_FLAGS = -std=c++20 -Og -march=native -g -fsanitize=address -Wall -Wextra -Isrc -DBIMODAL_DEBUG=ON
CC_FLAGS     = -std=c99 -O3 -march=native

# Source and target files
SRC_DIR   = src
MAIN_SRC  = $(SRC_DIR)/benchmark.cpp
FUZZ_SRC  = $(SRC_DIR)/fuzzer.cpp
LIBROPE_C = $(SRC_DIR)/librope/rope.c

TARGET_MAIN   = main
TARGET_FUZZER = fuzzer
LIBROPE_O     = $(SRC_DIR)/librope/rope.o

# Default to including librope
USE_LIBROPE ?= yes

# Conditionally set flags and dependencies for the main benchmark
ifeq ($(USE_LIBROPE), yes)
    MAIN_DEPS       = $(LIBROPE_O)
    MAIN_LINK_FLAGS = -DLIBROPE=ON
else
    MAIN_DEPS       =
    MAIN_LINK_FLAGS =
endif

.PHONY: all nolibrope run clean debug

# Default target: build main (with librope) and fuzzer
all: $(TARGET_MAIN) $(TARGET_FUZZER)

# Target to build without librope support
# Re-invokes make with USE_LIBROPE=no
nolibrope:
	@$(MAKE) all USE_LIBROPE=no

# Rule to build the main benchmark executable.
# Dependencies and flags change based on USE_LIBROPE.
$(TARGET_MAIN): $(MAIN_SRC) $(MAIN_DEPS)
	@echo "Compiling benchmark '$@'..."
	$(CXX) $(MAIN_FLAGS) $(MAIN_LINK_FLAGS) -o $@ $(MAIN_SRC) $(MAIN_DEPS)

# Rule to build the fuzzer executable
$(TARGET_FUZZER): $(FUZZ_SRC)
	@echo "Compiling fuzzer '$@'..."
	$(CXX) $(FUZZER_FLAGS) -o $@ $<

# Rule to build the librope object file
$(LIBROPE_O): $(LIBROPE_C) $(SRC_DIR)/librope/rope.h
	$(CC) $(CC_FLAGS) -c $< -o $@

# Rule to run the benchmark
run: $(TARGET_MAIN)
	./$(TARGET_MAIN)

# Rule to run the fuzzer in debug mode
debug: $(TARGET_FUZZER)
	./$(TARGET_FUZZER)

# Rule to clean up generated files
clean:
	@echo "Cleaning up..."
	rm -f $(TARGET_MAIN) $(TARGET_FUZZER) $(LIBROPE_O)
