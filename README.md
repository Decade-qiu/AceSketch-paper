# AceSketch

A compact C++17 implementation and benchmark for predicate-based distinct
cardinality estimation. `main.cpp` is the only executable entry point. The
repository contains AceSketch, the three baselines used by that entry point,
and their required utilities. It has no external library dependencies.

## Build

Use GCC or Clang with C++17 support:

```sh
make
./acesketch --help
```

Alternatively, with CMake installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/acesketch --help
```

Run experiments from the repository directory. Dataset paths and result paths
are resolved relative to the current working directory.

## Run

The five positional arguments retain the original experiment interface:

```sh
./acesketch <dataset> <query> <debug> <algorithm> <experiment>
```

| Argument | Values |
| --- | --- |
| `dataset` | `0`: CAIDA; `1`: ECB; `2`: Criteo |
| `query` | `0`: single predicate; `1`: OR; `2`: AND; `3`: inclusive range |
| `debug` | `0`: normal output; `1`: also show data samples and sketch parameters |
| `algorithm` | `0`: AceSketch; `1`: OmniSkt; `2`: HydraSkt; `3`: vHLL |
| `experiment` | Positive experiment ID used in the result filename |

Defaults are `0 0 0 0 1`, 16 MB of logical sketch memory, and random seed 2026.
Each invocation runs one experiment at one memory setting.

```sh
# CAIDA single-predicate experiment, using the default dataset layout.
./acesketch 0 0 0 0 1

# CAIDA AND experiment, with a chosen memory budget and seed.
./acesketch 0 2 0 0 2 --memory 4 --seed 42

# ECB range experiment, with explicitly supplied input files.
./acesketch 1 3 0 0 1 --memory 4 \
  --data-file /path/to/2019-Nov-upd.txt \
  --query-file /path/to/ecb_range_query_result.bak.txt
```

Optional flags are `--memory MB`, `--seed N`, `--data-root DIR`,
`--data-file FILE`, and `--query-file FILE`. The memory range is 0.01–1024 MB;
the seed must fit in an unsigned 32-bit integer. A fixed seed reproduces
estimation results on the same platform and toolchain; timing measurements vary.

### Supported benchmark configurations

| Algorithm | Datasets | Query types |
| --- | --- | --- |
| AceSketch | CAIDA, ECB | Single, OR, AND, range |
| AceSketch | Criteo | Single, OR, AND |
| OmniSkt | CAIDA, ECB | Single, AND, range |
| OmniSkt | Criteo | Single, AND |
| HydraSkt | CAIDA | Single, AND |
| vHLL | CAIDA | Single, AND |

The OmniSkt benchmark retains the original entry point's query selection.
HydraSkt and vHLL enumerate predicate combinations and are limited to the
four-predicate CAIDA configuration. Unsupported combinations fail before loading
data. This entry point does not add a general Boolean-expression parser.

## Input files

Datasets and query ground truths are supplied separately. With the default
`--data-root ..`, the expected layout is:

```text
<data-root>/
├── data_unique/caida_2019_5.txt
├── data_ecb/2019-Nov-upd.txt
├── data_criteo/day_0_compressed.txt
└── APCE_data/
    ├── caida_<query>_query_result.bak.txt
    ├── ecb_<query>_query_result.bak.txt
    └── criteo_<query>_query_result.bak.txt
```

Here `<query>` is `single`, `union`, `intersection`, or `range`; Criteo has no
range-query configuration. File overrides can point to data stored elsewhere.

Data fields are whitespace-separated and follow the original preprocessing:

| Dataset | Input layout | Normalization |
| --- | --- | --- |
| CAIDA | 5 fields: distinct-count key followed by 4 attributes | Reverse the fields, putting the key last |
| ECB | 9 fields: 7 attributes, distinct-count key, trailing unused field | Drop the last field; skip `NULL` attributes |
| Criteo | 18 fields: unused leading field, distinct-count key, 16 attributes | Drop the first field and move the key to the end |

After normalization, attribute indices start at zero and exclude the final
distinct-count key. Each nonempty attribute value is encoded as
`value,attribute_index` so identical values in different attributes remain
distinct predicates.

Predicate queries contain encoded predicates followed by the exact cardinality:

```text
value_a,0 120
value_a,0 value_b,2 450
```

The selected query type determines whether multiple predicates use OR or AND.
Single queries require one predicate. AND queries require distinct attribute
indices. Predicates are sorted by attribute index when loaded so the combination
baselines use consistent keys.

Range queries contain inclusive bounds followed by the exact cardinality:

```text
1,100 42
100,500 210
```

Range attributes are normalized index 3 for CAIDA and index 6 for ECB. Values
and bounds must be in `[1, 2^32 - 1]` for CAIDA or `[1, 2^12 - 1]` for ECB.
Missing, empty, or malformed files produce an error with the offending path and,
where applicable, line number.

## Results

Results are written to `result/<dataset>_<query>/<algorithm>/<experiment>.txt`.
Reusing the same IDs overwrites that result file. Each file contains the memory
setting followed by one row for the selected algorithm:

```text
Memory: <MB>MB
<algorithm> <ARE> <AAE> <MSE> <insert_Mops> <query_Mops> <sum:count> <sum:count> <sum:count> <sum:count>
```

ARE is mean absolute relative error, AAE is mean absolute error, and MSE is mean
squared error. As in the original entry point, zero ground truth contributes
zero to relative error and remains in the averaging denominator. Throughputs
are millions of records or queries per second. The four `sum:count` entries
contain relative-error sums and query counts for exact cardinalities `<=100`,
`101–1000`, `1001–10000`, and `>10000`; divide sum by count to obtain the mean
for a nonempty group.

Memory settings retain the original logical bit-budget formulas. Registers and
bitmaps use byte storage, with additional allocation overhead, so the setting
does not equal process resident memory. Input records, queries, and predictions
are also held in memory outside the sketch budget.

## Source layout

```text
main.cpp                  Experiment arguments, timing, errors, and output
algorithms/AceSketch.h    Shared metadata, predicate queries, range encoding
algorithms/omniSkt.h      OmniSkt baseline
algorithms/hydraSkt.h     HydraSkt baseline
algorithms/hll.h          Combination-based vHLL baseline
utils/data_reader.h      Dataset normalization and query loading
utils/hash.h             Required MurmurHash3 32-bit hashing
utils/hyperloglog.h       HLL metadata and estimator
utils/max_heap.h          Bounded sample heap for OmniSkt
utils/subpop.h            Predicate-combination mapping
```

AceSketch corresponds to `sHLL` in the original implementation. Its shared
metadata update, register reconstruction, optimized intersection estimate,
and range decomposition retain the original algorithms. The standalone version
removes unused variants, duplicate readers, global benchmark state, unused PCG
headers, commented experiments, debugging artifacts, and historical outputs.

Small correctness repairs accompany the cleanup: initialize OmniSkt bitmaps and
free range bitmaps; keep HydraSkt queries read-only and exclude the distinct-count
key from predicate combinations; avoid overflow in AceSketch collision
probabilities and range traversal; skip missing ECB attributes; initialize range
configuration for every dataset; and emit results only for the algorithm that
actually ran. These repairs can change estimates relative to the original
program's faulty paths. The estimator formulas and logical memory accounting
are retained.
