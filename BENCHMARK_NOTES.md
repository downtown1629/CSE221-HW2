# Benchmark Scenario Notes

This document summarizes the intent, workload model, and cursor/position distribution used in each benchmark. It is written to fit naturally into an ACM‑style report and aims to make the rationale behind each scenario clear to readers.

## Typing Mode – Insert
**Goal:** Model a user typing predominantly at a fixed region (e.g., near the middle of a document).  
**Data size:** 5 MB prefilled with `'x'`.  
**Workload:** 1,000 single‑character inserts at the midpoint.  
**Cursor distribution:** Fixed at the midpoint (worst‑case for contiguous arrays, best‑case for gap‑oriented structures).  
**Structures:** `std::vector`, `std::string`, gap buffer, piece table, rope, BiModalText.  
**Interpretation:** Measures local insertion cost; contiguous arrays pay O(N) shifts, while gap/cursor‑aware structures exploit locality.

## Typing Mode – Sequential Read
**Goal:** Measure raw sequential scan throughput with no edits.  
**Data size:** 100 MB prefilled with `'x'`.  
**Workload:** Full linear scan.  
**Cursor distribution:** Not applicable (sequential iteration).  
**Structures:** Same set as above.  
**Interpretation:** Isolates read bandwidth and iterator overhead; structure/layout differences are minimized.

## Heavy Typer
**Goal:** Stress sustained insertion in a large document.  
**Data size:** 100 MB prefilled with `'x'`.  
**Workload:** 5,000 single‑character inserts near the midpoint.  
**Cursor distribution:** Midpoint (reflecting focused typing; gap‑friendly).  
**Notes:** BiModalText runs `optimize()` after prefill; other structures have no analogous step.  
**Interpretation:** Highlights how each structure handles a large hot region; gap buffers excel, ropes rebalance, skiplist+gap sits between.

## Backspacer
**Goal:** Model repeated backspace over an existing document.  
**Data size:** 5 MB prefilled with `'x'`.  
**Workload:** 10,000 single‑character deletions at the midpoint.  
**Cursor distribution:** Fixed midpoint (local deletion).  
**Interpretation:** Tests deletion locality; contiguous arrays suffer shifts, gap/piece/rope/skiplist leverage structural edits.

## Refactorer (Random Read & Edit)
**Goal:** Mixed random reads and inserts over an existing document.  
**Data size:** 5 MB prefilled with `'x'`.  
**Workload:** 1,000 iterations; each iteration picks a uniform random position, reads a char, and inserts a char.  
**Cursor distribution:** Uniform random over current size (MT19937, fixed seed).  
**Interpretation:** Models refactoring/edits distributed throughout; penalizes structures with expensive random search.

## Random Cursor Movement & Insertion
**Goal:** Random access insertion without focused locality.  
**Data size:** 5 MB prefilled with `'x'`.  
**Workload:** 5,000 inserts at uniform random positions.  
**Cursor distribution:** Uniform random.  
**Notes:** For BiModalText, timing includes insert + optimize + scan; others only time the edit path.  
**Interpretation:** Measures robustness to scattered edits; higher sensitivity to search and maintenance costs.

---

### General Position/Randomness Policy
- Fixed seeds (MT19937) are used for reproducibility when randomness is involved.
- Prefill is uniform `'x'` data; chunked prefill is avoided except where noted.
- Cursor locality is chosen per scenario to match the intended user pattern: fixed for local typing/backspace, random for refactoring and scattered edits, sequential for pure reads.
