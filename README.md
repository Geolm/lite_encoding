# Lite Encoding Library

A header-only, high-performance adaptive entropy coding library written in C99.

## Overview

Lite Encoding implements a **Rice-Golomb** backend paired with a Move-To-Front (MTF) Alphabet. This combination captures categorical redundancy (repetitive patterns) and numerical sparsity. The library provides also small delta (signed) and small literal encoding functions with a soft K adaptation.

### MFT Heuristic

Unlike standard MTF, this library uses a low-pass promotion strategy (target = index >> 1). This filter prevents "alphabet thrashing" by requiring a symbol to appear multiple times before it can dominate the zero-index slot.  

### Soft K Adaptation
The library employs a "Soft K" mechanism to track data magnitude trends. Instead of switching the Rice parameter $k$ immediately upon seeing a large value, it maintains a `k_trend` counter.  

$k$ only increments or decrements when the trend exceeds `LE_K_TREND_THRESHOLD` (12). This heuristic ensures that the coder remains stable in the presence of noise while eventually adapting to new statistical regions in the bitstream.

---

## Core API


| Function | Usage |
|------:|------:|
| le_init     | Initializes the stream with a memory buffer.    |
| le_encode_symbol     | Encodes 8-bit data using the MTF alphabet. Best for repetitive patterns.    |
| le_encode_literal     | Encodes raw values directly via Rice coding. Best for small numbers. |
| le_encode_delta | Encodes signed differences using ZigZag + Rice. Best for small delta |


## Example

````C

#include "lite_encoding.h"

size_t compress_data(uint8_t* src, uint8_t* dst, size_t size)
{
    le_stream s;
    le_model m;

    le_init(&s, dst, size);
    le_model_init(&m);
    
    le_begin_encode(&s);
    for(size_t i = 0; i < size; ++i) {
        le_encode_symbol(&s, &m, src[i]);
    }
    return le_end_encode(&s);
}

````

