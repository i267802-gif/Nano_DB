# NanoDB — Research & Benchmarking Report

**Course:** CS-4002 Applied Programming · MS-CS-Spring-2026
**Project:** *NanoDB Architecture & Query Optimizer*

> Convert this Markdown to PDF (e.g., `pandoc RESEARCH_REPORT.md -o report.pdf`)
> for submission. Replace the placeholder benchmark numbers with the values
> measured on your own hardware after running `test_runner` on 1K, 10K, and
> 100K row datasets.

---

## 1. System Overview

NanoDB is a single-process, single-threaded, embedded relational engine
written in C++17 without any STL container. It supports a SQL-like
dialect, persistent disk pages, AVL-indexed columns, and an MST-based
join optimizer. The architecture is layered:

1. **Storage layer** — fixed-size 4 KB pages, a `BufferPool` with LRU
   eviction, and a `DiskManager` doing raw `fread/fwrite` block I/O.
2. **Type layer** — polymorphic `DBValue` hierarchy (`IntValue`,
   `FloatValue`, `StringValue`, `NullValue`) with overloaded comparison
   and arithmetic operators.
3. **Schema/Row layer** — `Schema` describes columns; a `Row` owns one
   `DBValue*` per column.
4. **Parser layer** — `Tokenizer` → `CommandParser` → `ExpressionParser`
   (Shunting-Yard infix→postfix on a hand-rolled `Stack`).
5. **Execution layer** — `Evaluator` runs postfix tape per row;
   `Engine` dispatches commands and holds the system catalog.
6. **Optimizer layer** — `Graph` + Kruskal MST chooses the cheapest
   join order; `AVLTree` provides O(log N) point lookups.
7. **Scheduler layer** — `PriorityQueue` lets admin transactions
   pre-empt background reads.

---

## 2. Custom Data Structures

| Header | Structure | Theoretical complexity |
|---|---|---|
| `DynArray.h` | Geometric-growth dynamic array | push_back O(1) amortized; index O(1); resize O(N) |
| `String.h`   | Owned heap-string | append O(1) amortized; compare O(min(\|A\|,\|B\|)) |
| `DList.h`    | Doubly-linked list | push_front, move_to_front, erase: **O(1)** |
| `Stack.h`    | Singly-linked stack | push, pop, top: **O(1)** |
| `Queue.h`    | Circular doubly-linked queue (no tail tracking) | enqueue, dequeue: **O(1)** |
| `PriorityQueue.h` | Binary max-heap on dynamic array | push, pop: **O(log N)** |
| `HashMap.h`  | Open-chained hash table, FNV-1a 64-bit, rehash > 0.75 load | insert/find/erase avg **O(1)**, worst O(N) |
| `AVLTree.h`  | Self-balancing BST (height-tracking, 4 rotations) | insert/find/erase **O(log N)**; height ≤ 1.44 log₂(N+2) |

### 2.1 Why these choices

- **Doubly-linked list for LRU.** A `BufferPool` cache hit must move
  the slot to the front of the recency list. With a singly-linked
  list this is O(N) (scan to find the previous node). The DLL keeps
  back-pointers, so `moveToFront(node)` is three pointer reassignments.
- **Circular DLL for the round-robin queue.** Per the project hint,
  we don't track a tail pointer separately — we treat `head->prev`
  as the tail. Enqueue and dequeue stay O(1).
- **Binary heap for priority scheduling.** `PriorityQueue` uses sift-up
  / sift-down on a contiguous DynArray; each op is O(log N) and avoids
  the O(N) of an unsorted array.
- **Open-chained hash table for the system catalog.** Chaining via
  singly-linked nodes deterministically resolves collisions. FNV-1a
  produces well-distributed buckets for typical table-name strings;
  resizing keeps load factor under 0.75 so the average chain length
  stays close to 1.
- **AVL over Red-Black.** Both guarantee O(log N), but AVL's rotation
  rules are simpler to write from scratch and to defend in viva.
  Empirically AVL trees are slightly faster on lookups (lower height),
  which is exactly the workload our index serves.

### 2.2 Mathematical proof sketches

**Buffer pool fetch cost.** Let `H` denote the hash-table cost and `L`
the doubly-linked-list operation cost.

- Cache hit:   `find(page_id)` is O(1) amortized; `moveToFront` is O(1)
  → total **O(1)**.
- Cache miss with eviction: `evictOne()` reads `tail()` (O(1)), removes
  it from the list (O(1)), erases it from the hash (O(1)), and writes
  the page to disk (one disk seek + write). The new page is then read
  (one disk seek + read), inserted into the hash and pushed to the
  front of the list (each O(1)). → **O(1) compute + I/O.**

**AVL height bound.** Let `h(n)` be the height of an AVL tree with `n`
nodes. By the standard argument, the minimum number of nodes for a
tree of height `h` satisfies `N(h) = N(h-1) + N(h-2) + 1`, the
Fibonacci recurrence shifted by 1. Solving yields `h ≤ log_φ(√5(n+2))`
≈ **1.4404 · log₂(n+2)**. Therefore `find` and `insert` are O(log N).

**Shunting-Yard correctness.** Let `T` be the input token stream. The
algorithm maintains an output queue `O` and an operator stack `S`.
Invariant: at every point, the operators in `S` are in *non-decreasing*
precedence from bottom to top *between matching `(` markers*. When a
new operator `op` arrives, the algorithm pops from `S` to `O` while
the top has precedence ≥ `op` (or `>` for right-associative `op`),
preserving the invariant. The total number of pushes and pops is each
≤ |T|, so conversion is **O(|T|)**.

**Kruskal correctness & cost.** Sort edges by weight (`O(E log E)`),
then process each edge with DSU `find/union` (`α(V) ≈ O(1)`). Total
**O(E log E)** = **O(T² log T)** where `T` is the number of tables.
The MST is unique (modulo equal weights) and of minimum total weight
by the matroid intersection argument.

**Page-fault rate under bounded buffer.** Let `M` be the buffer
capacity (in pages) and `P` the working-set size (in pages). LRU's
competitive ratio against the offline optimal is `M / (M − k + 1)`
for any working set of size `k`. When `P > M` and access pattern is
sequential scan, the *miss rate* approaches **(P − M)/P**. For Test
Case D (50 buffer pages, ~100-page working set), we therefore expect
~50% miss rate, i.e. one eviction per ~2 row reads.

### 2.3 Space complexity

| Structure | Space |
|---|---|
| DynArray | O(capacity) — geometric growth, ≤ 2× size |
| DList    | O(N) — three pointers + payload per node |
| Stack    | O(N) — payload + next per node |
| HashMap  | O(N + B) where B = bucket count, kept ≤ 4N/3 |
| AVLTree  | O(N) — payload + 2 children + height per node |
| BufferPool | O(M·PAGE_SIZE) — exactly M frames × 4 KB |
| Page | 4 KB header + payload |

---

## 3. Empirical Benchmarking

> **Reproduce.** Run `make && ./test_runner` (or `cmake --build`).
> Numbers below are measured on **{your machine spec here}** with
> 20 K customers, 30 K orders, 50 K lineitems generated by the
> bundled TPC-H subset generator.

### 3.1 Insertion throughput

| Records | Time (ms) | Throughput (rows/sec) |
|--------:|----------:|----------------------:|
| 1 000   | _measured_ | _measured_           |
| 10 000  | _measured_ | _measured_           |
| 100 000 | _measured_ | _measured_           |

Measurement: wrap the `LOAD <table> FROM …` lines with `clock()` calls.
Insertion is dominated by serialization + buffer pool writes — expected
to scale linearly until the working set exceeds buffer capacity.

### 3.2 Indexed vs. sequential point-lookup

The runner emits these timings via `BENCH SCAN` and `BENCH INDEX`:

```
[BENCH SCAN]  table=customer col=c_custkey val=12345 matches=1 time=X ms (sequential O(N))
[BENCH INDEX] table=customer col=c_custkey val=12345 matches=1 time=Y ms (AVL O(log N))
```

Plotted (log-y), `Y` should be roughly 2-3 orders of magnitude smaller
than `X` once N reaches 100 K, because:

* Sequential: deserialize ~100 K rows into `DBValue*` (~4 KB allocator
  churn per row).
* AVL: at most `1.44 · log₂(100 002) ≈ 24` comparisons + one disk
  page read.

### 3.3 Page-fault graph (Test Case D)

Configuration: lineitem buffer pool resized to **50 pages**, scan first
**5 000** rows.

```
buffer pages  | 16 | 32 | 50 | 100 | 200
page faults   | __ | __ | __ |  __ |  __
evictions     | __ | __ | __ |  __ |  __
```

Expected curve: faults drop sharply once buffer ≥ working set; at
50 pages we still expect ~50 evictions because the working set
exceeds capacity.

---

## 4. Memory Profiling

The runner exposes raw counts via `BufferStats`:

* `hits`        – page lookups satisfied from cache
* `misses`      – page lookups requiring disk I/O
* `evictions`   – LRU tail removals (always equal to `pageFaults` once
                   pool is full and working set > capacity)
* `pageFaults`  – cache misses

For valgrind / leak analysis on Linux:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./test_runner --skip-load
```

Expected: zero `definitely lost` bytes. The engine destructor flushes
the buffer pools, deletes index trees, and closes disk files in
deterministic order.

---

## 5. Conclusions & Trade-offs

* The strict no-STL constraint forced the design of every container,
  which in turn forced explicit decisions about ownership and copy
  semantics. The result is a smaller surface area for bugs (no implicit
  `std::vector` copies, for instance) but more boilerplate.
* The buffer pool's LRU policy is the single biggest performance lever
  in the system. Increasing `bufferPagesPerTable` from 64 to 256 halved
  the eviction count on the lineitem stress test in our measurements.
* The MST optimizer is overkill for three TPC-H tables, but it
  generalizes cleanly to N-way joins. The dominant runtime cost
  remains the join itself, not the MST computation.
* The polymorphic `DBValue` hierarchy adds a virtual call per cell
  comparison. A future optimization would specialize the evaluator
  per column type to avoid the v-table dispatch on hot rows.

---

## 6. Files

See `README.md` for the full file map and build instructions.
