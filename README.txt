====================================================
CSE221 Data Structure - Homework #2: README
====================================================

* Name: Seungmin Lee
* Student ID: 20241272

----------------------------------------------------

1. Description of Data Structure

The data structure designed and implemented in this project is the Bi-Modal Skip List.

This data structure aims to efficiently handle two conflicting workloads faced by modern text editors: frequent edits (Write-Heavy) and large-scale analysis (Read-Heavy). To achieve this, it adopts a hybrid architecture that combines the fast random access of a Skip List (O(log N)) with the excellent local editing performance of a Gap Buffer (O(1)).

The core idea is the 'Bi-Modal' node. Each node can dynamically change its internal memory layout depending on the system's state:
- Edit Mode (GapNode): Maintains a Gap Buffer internally to react quickly to user input with minimal data movement.
- Analysis Mode (CompactNode): When optimize() is called, all nodes remove their gaps and transform into a physically contiguous memory array (Compact Array). This maximizes CPU cache and hardware prefetcher efficiency, resulting in sequential read performance that surpasses even std::vector.

Through this dynamic reconfiguration, the Bi-Modal Skip List provides balanced, high performance that is not skewed toward a single specific workload.

2. List of Submitted Files

/
├── src/
│   ├── BiModalSkipList.hpp  # Core implementation of the Bi-Modal Skip List.
│   ├── Nodes.hpp            # Struct definitions for Node, GapNode, and CompactNode.
│   ├── Baselines.hpp        # Implementations of baseline data structures (Gap Buffer, Piece Table).
│   ├── benchmark.cpp        # The main benchmark program to measure and compare performance.
│   ├── fuzzer.cpp           # Fuzzing and correctness verification program against std::string.
│   └── librope_wrapper.cpp  # C++ wrapper for the `librope` C library for benchmarking.
├── librope/
│   └── (source files)       # Source code for the third-party `librope` C library, used as a baseline.
├── report.pdf               # Final project report detailing the design, analysis, and results.
├── Makefile                 # Build script for compiling all executables.
└── README.txt               # This file.

3. Compilation and Execution

3.1. Compilation

You can use the provided Makefile to compile all executables.

- Build all executables (main, fuzzer):
  This is the default command and includes librope in the benchmark.
  
  make
  
  or
  
  make all

- Build all executables WITHOUT librope:
  Compiles the benchmark main without librope support.
  
  make nolibrope

- Build only the fuzzer (with Debug and Sanitizer flags):
  Builds the fuzzer in debug mode with AddressSanitizer enabled for memory error detection.
  
  make debug
  
  or
  
  make fuzzer

- Clean up generated files:
  
  make clean

3.2. Execution

- Run the benchmark (main):
  You can use the make run command or execute the main binary directly.
  
  make run
  
  or
  
  ./main
  
  The main executable can take arguments to run specific tests:
  - ./main [scenario]: Runs only a specific scenario (a-f). (e.g., ./main b runs only the sequential read test).
  - ./main [scenario] [structure]: Runs a specific scenario for a single data structure. (e.g., ./main d bimodal).

- Run the correctness verifier and fuzzer (fuzzer):
  Execute the fuzzer binary directly.
  
  ./fuzzer [iterations]
  
  - The [iterations] argument is optional. If omitted, it defaults to 5000 iterations.
  - The fuzzer uses std::string as an oracle to verify the correctness of all operations in BiModalText (insert, erase, optimize, random access, etc.).

4. Reproducing Key Experiments

The key performance results presented in the report (report.pdf) can be reproduced using the main executable. Below are the example commands for each scenario.

- Scenario A (Typing Mode - Insert):
  
  ./main a

- Scenario B (Sequential Read):
  
  ./main b

- Scenario C (The Heavy Typer):
  
  ./main c

- Scenario D (The Refactorer - Random Read & Edit):
  
  ./main d

- Scenario E (Random Cursor Movement & Insertion):
  
  ./main e

- Scenario F (The Paster):
  
  ./main f

- Run all benchmark scenarios:
  
  ./main

Expected Output

When you run a benchmark command, the name of each data structure and the time taken (in ms) for that scenario will be printed to the console in a table format.
For example, running ./main b will produce output similar to the following (exact numbers may vary depending on your system):

[Scenario B: Sequential Read (best of 10)]
  - N=100MB
--------------------------------------------------------------
Structure         Read (ms)      Note
--------------------------------------------------------------
std::vector       1.806000       (Contiguous array, sequential scan)
std::string       1.835000       (Baseline contiguous, sequential scan)
...
BiModalText       1.655000       (Skiplist nodes, span scan)

When running ./fuzzer, it will print its progress. If all tests pass, it will display a final success message. If an error is found, the program will exit with detailed information about the failure.

...
[REGRESSION] Running deterministic tests...
  ✓ simple sanity tests passed
  ✓ split/merge stress test passed
  ✓ random test seed=1 ops=2000 passed
...
==================================================
✓ ALL TESTS & FUZZING PASSED
==================================================
