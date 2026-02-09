/*

zlib License

(C) 2026 Geolm

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

*/




/*

Lite Encoding Library

A high-performance, adaptive entropy coding library designed for 
real-time compression tasks (e.g., texture transcoding, delta signaling).

  - Backend: Adaptive Rice-Golomb Coder
        Maps integers to variable-length bitstrings. The 'k' parameter 
        adapts dynamically via a "soft-trend" mechanism to track changes 
        in data magnitude without oscillating.
 
  - Frontend: MTF (Move-To-Front) Alphabet
        Maps 8-bit symbols to Rice indices. Uses a low-pass filter (index/2) 
        during promotion to prevent high-frequency jitter in the alphabet 
        ranking, ensuring stability in textures with localized noise.

  - Bitstream: 64-bit Reservoir
        Provides fast bit-level I/O by buffering data into a 64-bit word, 
        reducing the frequency of byte-level memory access.

USAGE:
 - Use le_encode_symbol() for data with categorical redundancy (repeated patterns).
 - Use le_encode_delta() for **small** numerical gradients or offsets.
 - Use le_encode_literal() for small numbers

 */


#ifndef LITE_ENCODING_H
#define LITE_ENCODING_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#define LE_ALPHABET_SIZE (256)
#define LE_K_TREND_THRESHOLD (12)
#define LE_Q_ESCAPE_SIZE (10)

static const uint8_t q_escape_for_k[LE_Q_ESCAPE_SIZE] = {16, 10, 4, 6, 255, 255, 255, 255, 255, 255};

enum le_mode
{
    le_mode_idle,
    le_mode_encode,
    le_mode_decode
};

typedef struct le_stream
{
    uint8_t* buffer;
    uint32_t bit_offset;
    size_t position;
    size_t size;

    uint64_t bit_reservoir;
    uint32_t bits_available;

    enum le_mode mode;
} le_stream;

typedef struct le_model
{
    uint8_t alphabet[LE_ALPHABET_SIZE];
    uint8_t k;  // rice k-value
    int8_t k_trend;
} le_model;

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_refill(le_stream* s)
{
    // Pull bytes into the reservoir until it's full enough for any standard read
    while (s->bits_available <= 56 && s->position < s->size)
    {
        s->bit_reservoir |= ((uint64_t)s->buffer[s->position]) << s->bits_available;
        s->bits_available += 8;
        s->position++;
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_flush(le_stream* s)
{
    while (s->bits_available >= 8)
    {
#ifdef LE_CHECKS
        assert(s->position < s->size);
#endif
        s->buffer[s->position] = (uint8_t)(s->bit_reservoir & 0xFF);
        s->bit_reservoir >>= 8;
        s->bits_available -= 8;
        s->position++;
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_init(le_stream *s, void* buffer, size_t size)
{
#ifdef LE_CHECKS
    assert(s != NULL);
    assert(buffer != NULL);
#endif
    s->buffer = (uint8_t*)buffer;
    s->size = size;
    s->position = 0;
    s->bit_reservoir = 0;
    s->bits_available = 0;
    s->mode = le_mode_idle;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_begin_encode(le_stream* s)
{
    s->position = 0;
    s->bit_reservoir = 0;
    s->bits_available = 0;
    s->mode = le_mode_encode;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline size_t le_end_encode(le_stream* s)
{
    while (s->bits_available > 0)
    {
        if (s->position < s->size)
        {
            s->buffer[s->position] = (uint8_t)(s->bit_reservoir & 0xFF);
            s->bit_reservoir >>= 8;
            s->position++;
        }
        
        if (s->bits_available > 8)
        {
            s->bits_available -= 8;
        }
        else
        {
            s->bits_available = 0;
        }
    }

    s->mode = le_mode_idle;
    return s->position;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_begin_decode(le_stream* s)
{
    s->position = 0;
    s->bit_reservoir = 0;
    s->bits_available = 0;
    s->mode = le_mode_decode;
    le_refill(s);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_end_decode(le_stream* s)
{
    s->mode = le_mode_idle;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_write_bits(le_stream* s, uint8_t data, uint8_t num_bits)
{
    s->bit_reservoir |= (uint64_t)(data & ((1 << num_bits) - 1)) << s->bits_available;
    s->bits_available += num_bits;
    if (s->bits_available >= 32)
        le_flush(s);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_read_bits(le_stream* s, uint8_t num_bits)
{
    if (s->bits_available < num_bits)
        le_refill(s);

    uint8_t value = (uint8_t)(s->bit_reservoir & ((1U<<num_bits)-1U));
    s->bit_reservoir >>= num_bits;
    s->bits_available -= num_bits;
    return value;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_write_byte(le_stream* s, uint8_t value)
{
    s->bit_reservoir |= ((uint64_t)value << s->bits_available);
    s->bits_available += 8;
    if (s->bits_available >= 32)
        le_flush(s);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_read_byte(le_stream* s)
{
    if (s->bits_available < 8)
        le_refill(s);

    uint8_t value = (uint8_t)(s->bit_reservoir & 0xFF);
    s->bit_reservoir >>= 8;
    s->bits_available -= 8;
    return value;
}


// ----------------------------------------------------------------------------------------------------------------------------
void le_model_init(le_model *model)
{
    for(uint32_t i=0; i<LE_ALPHABET_SIZE; ++i)
        model->alphabet[i] = i;
    model->k = 2;
    model->k_trend = 0;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void rice_encode(le_stream *s, uint32_t value, uint8_t k) 
{
#ifdef LE_CHECKS
    assert(k < LE_Q_ESCAPE_SIZE);
#endif

    uint32_t q = value >> k;
    uint32_t q_limit = q_escape_for_k[k];
    uint32_t r = value & ((1U << k) - 1U);

    // checks if raw value is cheaper
    q = (q >= q_limit) ? q_limit : q;

    // write q
    for (uint32_t i = 0; i < q; ++i)
        le_write_bits(s, 1, 1);
    
    // terminator '0'
    le_write_bits(s, 0, 1);

    // remainder or rawbyte
    if (q == q_limit)
        le_write_byte(s, value);
    else if (k > 0) 
        le_write_bits(s, (uint8_t)r, k);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t rice_decode(le_stream *s, uint8_t k) 
{
    uint32_t q = 0;
    uint32_t q_limit = q_escape_for_k[k];

    while (true) 
    {
        if (s->bits_available == 0) 
            le_refill(s);
        
        if ((s->bit_reservoir & 1ULL) != 0) 
        {
            q++;
            s->bit_reservoir >>= 1;
            s->bits_available--;
        } 
        else 
        {
            s->bit_reservoir >>= 1; 
            s->bits_available--;
            break;
        }
    }

    if (q == q_limit)
        return le_read_byte(s);

    if (s->bits_available < k)
        le_refill(s);
    
    uint32_t r = 0;
    if (k > 0) 
    {
        r = (uint32_t)(s->bit_reservoir & ((1ULL << k) - 1U));
        s->bit_reservoir >>= k;
        s->bits_available -= k;
    }

    return (uint8_t) ((q << k) | r);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_model_update(le_model* model, uint8_t value)
{
    if (value < (1U << model->k) && model->k > 0) 
        model->k_trend--;
    else if (value > (3U << model->k) && model->k < 7) 
        model->k_trend++;

    // soft adaptation
    if (model->k_trend > LE_K_TREND_THRESHOLD)
    {
        model->k++;
        model->k_trend = 0;
    }
    else if (model->k_trend < -LE_K_TREND_THRESHOLD)
    {
        model->k--;
        model->k_trend = 0;
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_encode_symbol(le_stream *s, le_model *model, uint8_t value)
{
#ifdef LE_CHECKS
    assert(s->mode == le_mode_encode);
#endif

    uint32_t index = 0;
    for (; index < 256; index++)
        if (model->alphabet[index] == value)
            break;

    rice_encode(s, index, model->k);

    // move up this value in the alphabet
    if (index > 0) 
    {
        uint8_t temp = model->alphabet[index];
        uint32_t target_index = index / 2;  // lowpass filter, prevent jittering

        for (uint32_t i = index; i > target_index; i--)
            model->alphabet[i] = model->alphabet[i - 1];
        
        model->alphabet[target_index] = temp;
    }

    le_model_update(model, index);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_decode_symbol(le_stream *restrict s, le_model *restrict model) 
{
#ifdef LE_CHECKS
    assert(index < LE_ALPHABET_SIZE);
#endif

    uint8_t index = rice_decode(s, model->k);
    uint8_t value = model->alphabet[index];

     // move up this value in the alphabet
    if (index > 0) 
    {
        uint8_t temp = model->alphabet[index];
        uint32_t target_index = index / 2;  // lowpass filter, prevent jittering

        for (uint32_t i = index; i > target_index; i--)
            model->alphabet[i] = model->alphabet[i - 1];
        
        model->alphabet[target_index] = temp;
    }

    le_model_update(model, index);

    return value;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t zigzag8_encode(int8_t v)
{
    return (uint8_t)((v << 1) ^ (v >> 7));
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline int8_t zigzag8_decode(uint8_t v)
{
    return (int8_t)((v >> 1) ^ -(int8_t)(v & 1));
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_encode_literal(le_stream *s, le_model* model, uint8_t value)
{
    rice_encode(s, value, model->k);
    le_model_update(model, value);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_decode_literal(le_stream* s, le_model* model)
{
    uint8_t value = rice_decode(s, model->k);
    le_model_update(model, value);
    return value;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_encode_delta(le_stream *s, le_model* model, int8_t delta)
{
    uint8_t zz = zigzag8_encode(delta);
    rice_encode(s, zz, model->k);
    le_model_update(model, zz);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline int8_t le_decode_delta(le_stream* s, le_model* model)
{
    uint8_t zz = rice_decode(s, model->k);
    le_model_update(model, zz);
    return zigzag8_decode(zz);
}

#endif

