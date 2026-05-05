# NanoDB — A From-Scratch Mini Database Engine

**Course:** CS-4002 Applied Programming · MS-CS-Spring-2026
**Project:** *NanoDB Architecture & Query Optimizer*

NanoDB is a tiny C++17 database system, built end-to-end without a single
STL container. It parses a SQL-like dialect, plans multi-table joins via
a Minimum Spanning Tree, indexes integer columns with a self-balancing
AVL tree, manages a fixed buffer pool with a custom doubly-linked LRU
list, and serializes pages to disk as raw 4 KB binary blocks.

> **GitHub:** _add your repo URL here_

---

## 1. Architecture at a glance

```
+------------------------- Engine -----------------------------+
|   System Catalog (HashMap<table_name, Table*>)               |
|   PriorityQueue<ScheduledQuery>  (admin > background)        |
|                                                              |
|     +-- CommandParser -- ExpressionParser (Shunting Yard)    |
|     |       (custom Stack)                                   |
|     +-- Evaluator (postfix RPN)                              |
|     +-- JoinOptimizer (Graph + Kruskal MST)                  |
|     +-- Table[ ]                                             |
|           +-- Schema  (DynArray<Column>)                     |
|           +-- AVLTree<int, RowLocator>  (per indexed column) |
|           +-- BufferPool                                     |
|                +-- DList<int>  (LRU recency)                 |
|                +-- HashMap<page_id, slot>                    |
|                +-- Page[capacity]                            |
|           +-- DiskManager  (FILE* binary I/O on PAGE_SIZE)   |
+--------------------------------------------------------------+
```

Every container in the diagram is hand-written in `include/`:

| File | Container | Used by |
|------|-----------|---------|
| `String.h/.cpp` | Heap-backed string | Everywhere |
| `DynArray.h` | Vector with placement new | Schema, Row, Engine results |
| `DList.h` | Doubly-linked list | Buffer pool LRU recency list |
| `Stack.h` | Singly-linked stack | Shunting-Yard parser, evaluator |
| `Queue.h` | Circular doubly-linked queue | Reusable FIFO primitive |
| `PriorityQueue.h` | Binary max-heap | Scheduling admin > background queries |
| `HashMap.h` | Chained hash table | System catalog, buffer index, indexed-set |
| `AVLTree.h` | Self-balancing BST | Per-column secondary indexes |

---

## 2. Build & Run

### Option A — CMake (recommended)

```bash
cd NanoDB
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
# Binary lands in build/ (or build/Release on multi-config generators).

# Run the test harness:
./build/test_runner            # Linux / macOS
build\Release\test_runner.exe  # Windows MSVC
```

### Option B — GNU Make

```bash
cd NanoDB
make
./test_runner
```

Either path produces:

* `test_runner` — the automated harness (entry point for grading).
* `gen_tpch`   — a standalone TPC-H subset generator (optional;
  the test runner generates data internally if missing).

### What the runner does

1. Opens `nanodb_execution.log`.
2. Generates `data/customer.tbl`, `data/orders.tbl`, `data/lineitem.tbl`
   if not already present (20K + 30K + 50K rows = 100K total).
3. Reads `queries.txt` and runs each line as a NanoDB command, printing
   results and emitting log entries for every cache eviction, postfix
   conversion, MST routing decision, etc.
4. Runs all seven explicit demo test cases A–G described below.
5. Prints buffer-pool statistics for every table.

### Optional flags

```text
test_runner --queries queries.txt   # workload file
            --data    data          # data directory
            --log     nanodb_execution.log
            --buffer  64            # default buffer pages per table
            --no-gen                # skip TPC-H generation
            --skip-load             # skip LOAD lines (smoke test)
```

---

## 3. SQL-like dialect

```
CREATE TABLE <name> (col TYPE, col TYPE, ...)
INSERT INTO <name> VALUES (v1, v2, ...)
SELECT [col[, col]* | *] FROM <t> [JOIN <t> [JOIN <t>]] [WHERE <expr>]
UPDATE <t> SET <col> = <value> [WHERE <expr>]
DELETE FROM <t> [WHERE <expr>]

LOAD <t> FROM "<path>"        -- bulk-load TPC-H .tbl file
INDEX <t> ON <col>            -- build AVL index

BENCH SCAN  <t> <col> <val>   -- forced sequential scan timing
BENCH INDEX <t> <col> <val>   -- AVL index lookup timing

ADMIN <stmt>                  -- mark statement as high-priority
```

`<expr>` supports `+ - * / %`, `== != < <= > >=`, `AND OR NOT`, parentheses,
integer / float / quoted-string literals, and column references.

Types: `INT`, `FLOAT` (a.k.a. `DOUBLE`/`DECIMAL`), `STRING` (a.k.a. `VARCHAR`/`TEXT`/`CHAR`).

---

## 4. Demo Test Cases (mapped to spec)

The runner exercises each test case automatically and logs evidence to
`nanodb_execution.log`.

| # | Spec test case | What the runner does |
|---|---|---|
| **A** | Parser & Evaluator | Runs `SELECT * FROM customer WHERE (c_acctbal > 5000 AND c_mktsegment == "BUILDING") OR c_nationkey == 15` and logs the **postfix tape** generated by the custom Stack. |
| **B** | Index Optimizer | Runs `BENCH SCAN` then `BENCH INDEX` against the same key and prints both timings. The reduction is the visible proof of O(log N) lookup. |
| **C** | Join Optimizer | Runs `SELECT * FROM customer JOIN orders JOIN lineitem` and prints the **MST path** Kruskal selected. |
| **D** | Memory Stress | Resizes the lineitem buffer to **50 pages**, scans **5,000** records, and prints `evictions` from the LRU. |
| **E** | Priority Queue | Submits 50 background SELECTs and one `ADMIN UPDATE` to the priority queue, then drains and verifies the admin query ran first. |
| **F** | Deep Expression | Evaluates `((o_totalprice * 1.5) > 100000 AND (o_custkey % 2 == 0)) OR (o_orderstatus != "O")` to confirm operator precedence and `% * / +` work on the postfix evaluator. |
| **G** | Durability | Inserts 5 sentinel customers and flushes pages. Re-running the runner after termination loads the same rows back from disk. |

---

## 5. Complexity summary

| Structure | Operation | Complexity |
|---|---|---|
| `DynArray` | push_back amortized | **O(1)** |
| `DList` | push_front / move_to_front / erase | **O(1)** |
| `Stack` / `Queue` | push / pop | **O(1)** |
| `PriorityQueue` | push / pop | **O(log N)** |
| `HashMap` | insert / find / erase (avg) | **O(1)** |
| `AVLTree` | insert / find / erase | **O(log N)** |
| Buffer pool fetch — hit | hash + DLL move-to-front | **O(1)** |
| Buffer pool fetch — miss | hash + LRU pop tail + disk read | **O(1) + I/O** |
| Sequential scan | `forEachRow` over all pages | **O(N)** |
| Indexed lookup | AVL find | **O(log N)** |
| Join optimizer | Kruskal MST on T tables | **O(T² log T)** |
| Postfix conversion (Shunting-Yard) | per token | **O(1)** amortized |

See the research report (`docs/`) for full proofs and benchmarking graphs.

---

## 6. File map

```
NanoDB/
├─ CMakeLists.txt
├─ Makefile
├─ README.md
├─ .gitignore
├─ queries.txt                <- 50-query TPC-H workload
├─ test_runner.cpp            <- automated harness
├─ tools/gen_tpch.cpp         <- standalone TPC-H generator
├─ include/                   <- headers (containers + engine)
│   ├─ String.h, DynArray.h, DList.h, Stack.h, Queue.h
│   ├─ PriorityQueue.h, HashMap.h, AVLTree.h
│   ├─ DBValue.h, Schema.h, Row.h
│   ├─ Page.h, BufferPool.h, DiskManager.h, Logger.h
│   ├─ Tokenizer.h, Parser.h, Evaluator.h
│   ├─ Graph.h, Table.h, Engine.h
└─ src/                       <- implementations
```

---

## 7. STL ban — verified

`grep -RIn 'std::vector\|std::list\|std::map\|std::stack\|std::queue\|std::set\|std::unordered' include src test_runner.cpp tools` returns nothing.
The only `std::` references are non-container utilities: `std::FILE`,
`std::strlen`, `std::memcpy`, `std::time`, `std::printf`, `std::clock`, etc.
