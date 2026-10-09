# Lite Encoding Library

A header-only, adaptive entropy coding library written in C11.

## Overview

Lite Encoding implements a **Rice-Golomb** backend paired with a Move-To-Front (MTF) Alphabet. This combination captures categorical redundancy (repetitive patterns) and numerical sparsity. The library also provides small delta (signed) and small literal encoding functions with a soft K adaptation.

Data flows through a 64-bit bit reservoir, so bit-level I/O is buffered into a single word instead of hitting memory byte by byte.

### MTF Heuristic

Unlike standard MTF, this library uses a low-pass promotion strategy (target = index / 2). This filter prevents "alphabet thrashing" by requiring a symbol to appear multiple times before it can dominate the zero-index slot.

### Soft K Adaptation

The library employs a "Soft K" mechanism to track data magnitude trends. Instead of switching the Rice parameter $k$ immediately upon seeing a large value, it maintains a `k_trend` counter.

$k$ only increments or decrements when the trend exceeds `LE_K_TREND_THRESHOLD` (12). This heuristic ensures that the coder remains stable in the presence of noise while eventually adapting to new statistical regions in the bitstream.

### Rice Coder & Escape

A value is split into a quotient $q = \texttt{value} \gg k$ and a remainder $r = \texttt{value} \& ((1 \ll k) - 1)$, written as $q$ ones, a single zero, then $k$ bits of $r$.

Values with $q \ge \texttt{q\_escape}$ are written in "escape" form instead: $\texttt{q\_escape}$ ones, a zero, and the full value as a raw byte. This bounds the worst-case cost and keeps every read inside the 64-bit reservoir:

- **Dynamic models** use the fixed `q_escape_for_k` table (4 for $k \le 6$, i.e. no escape; 255 otherwise).
- **Static models** store their own `q_escape` in the model, chosen jointly with $k$ at init time to minimize total bits for the given histogram.

---

## Models

| Function | Usage |
|------:|------:|
| `le_dynamic_model_init(model)` | Adaptive model: identity alphabet, $k=2$. Adapts via MTF promotion + soft K. |
| `le_static_model_init(model, histogram, num_symbols)` | Builds a sorted alphabet from a user histogram and searches the best $(k, \texttt{q\_escape})$ pair. Never adapts. |
| `le_static_model_load(model, alphabet, num_symbols, k, q_escape)` | Decoder side: reconstructs a static model from the serialized state (one byte per alphabet entry, plus $k$ and $\texttt{q\_escape}$). |

Maximize efficiency through specialization: use **multiple** model instances to track different data streams. One model per data type ensures the history remains relevant and the compression stays tight.

## Core API

| Function | Usage |
|------:|------:|
| `le_encode_symbol` / `le_decode_symbol` | Encodes 8-bit data through the MTF alphabet. Best for repetitive patterns. |
| `le_encode_literal` / `le_decode_literal` | Encodes raw values directly via Rice coding. Best for small numbers. |
| `le_encode_delta` / `le_decode_delta` | Encodes signed differences via ZigZag + Rice. Best for small deltas. |

Raw bit/byte accessors (`le_write_bits`, `le_read_bits`, `le_write_byte`, `le_read_byte`) are also exposed for custom payloads.

## Stream Lifecycle

```
le_init(&s, buffer, size);
le_begin_encode(&s);   // or le_begin_decode(&s)
... encode / decode ...
le_end_encode(&s);     // returns number of bytes written (0 on error)
// le_end_decode(&s);
```

Always check `s.status == LE_OK` after `le_end_encode` (it returns 0 and sets `LE_BUFFER_OVERRUN` if the buffer was too small), and after the decoding loop to catch truncated streams.

## Examples

Dynamic model, encoding:

````C
#include "lite_encoding.h"

size_t compress_data(uint8_t* src, uint8_t* dst, size_t size)
{
    le_stream s;
    le_model m;

    le_init(&s, dst, size);
    le_dynamic_model_init(&m);

    le_begin_encode(&s);
    for (size_t i = 0; i < size; ++i)
        le_encode_symbol(&s, &m, src[i]);

    return le_end_encode(&s);
}
````

Dynamic model, decoding (the decoder model must start from the same initial state):

````C
size_t decompress_data(uint8_t* src, uint8_t* dst, size_t src_size, size_t size)
{
    le_stream s;
    le_model m;

    le_init(&s, src, src_size);
    le_dynamic_model_init(&m);

    le_begin_decode(&s);
    for (size_t i = 0; i < size; ++i)
        dst[i] = le_decode_symbol(&s, &m);
    le_end_decode(&s);

    return s.status == LE_OK ? size : 0;
}
````

Static model: build once from a histogram, serialize the small state, load it on the decoder side:

````C
// Encoder side
le_model m;
le_static_model_init(&m, histogram, num_symbols);
// state to ship: m.alphabet[0..m.num_symbols), m.k, m.q_escape, m.num_symbols

// Decoder side
le_model m;
le_static_model_load(&m, received_alphabet, num_symbols, k, q_escape);
````

## Building & Testing

````sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/test
````

The test suite uses [Greatest](https://github.com/silentb1t/greatest) and roundtrips every coder against deterministic data (font atlas, ramps, cycles, random, boundary values).

## License

Zlib — see [LICENSE](LICENSE).
