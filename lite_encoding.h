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


#ifndef LITE_ENCODING_H
#define LITE_ENCODING_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#define LE_MODEL_MAX_HOT (48)
#define LE_HISTOGRAM_SIZE (256)
#define le_min(a, b) ((a < b) ? a : b)

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
    uint8_t hot_values[LE_MODEL_MAX_HOT];
    uint8_t k;  // rice k-value
    uint8_t num_hot_values;
} le_model;

typedef struct le_histogram
{
    uint32_t count[LE_HISTOGRAM_SIZE];
    uint32_t num_symbols;
} le_histogram;

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

//----------------------------------------------------------------------------------------------------------------------------
static inline void histogram_init(le_histogram* h, uint32_t num_symbols)
{
#ifdef LE_CHECKS
    assert(num_symbols > 3 && num_symbols <= 256);
#endif

    h->num_symbols = num_symbols;
    for(uint32_t i=0; i<num_symbols; ++i)
        h->count[i] = 0;
}

//----------------------------------------------------------------------------------------------------------------------------
// calculates the best Rice parameter 'k' for a given distribution.
uint32_t compute_best_k(const uint32_t *histogram, uint8_t *hot_values, uint32_t hot_used)
{
    uint32_t best_k = 0;
    uint64_t min_total_bits = UINT64_MAX;

    
    for (uint32_t k = 0; k <= 4; k++)
    {
        uint64_t current_k_bits = 0;

        for (uint32_t i = 0; i < UINT8_MAX+1; i++)
        {
            uint32_t count = histogram[i];
            if (count == 0) 
                continue;

            uint32_t table_idx = UINT32_MAX;
            for (uint32_t j = 0; j < hot_used; j++)
            {
                if (hot_values[j] == (uint8_t)i)
                {
                    table_idx = j;
                    break;
                }
            }

            if (table_idx == UINT32_MAX)
            {
                // not in hot table: 1 flag bit + 8 raw bits
                current_k_bits += (uint64_t)count * 9;
            }
            else
            {
                // calculate Rice length + 1 flag bit
                uint32_t q = table_idx >> k;
                uint32_t rice_bits = (q + 1) + k;
                uint32_t total_bits = 1 + rice_bits;

                if (total_bits > 9)
                    current_k_bits += (uint64_t)count * 9;
                else
                    current_k_bits += (uint64_t)count * total_bits;
            }
        }

        if (current_k_bits < min_total_bits)
        {
            min_total_bits = current_k_bits;
            best_k = k;
        }
    }

    return best_k;
}

// ----------------------------------------------------------------------------------------------------------------------------
void le_model_init(le_model *model, const uint32_t *histogram)
{
    memset(model->hot_values, 0, sizeof(model->hot_values));

    uint32_t selected[LE_MODEL_MAX_HOT];
    for (uint32_t i = 0; i < LE_MODEL_MAX_HOT; i++)
        selected[i] = UINT32_MAX;

    model->num_hot_values = 0;
    for (uint32_t i = 0; i < LE_MODEL_MAX_HOT; i++)
    {
        uint32_t max_count = 0;
        uint32_t max_index = UINT32_MAX;

        for (uint32_t s = 0; s < UINT8_MAX+1; s++)
        {
            bool already = false;
            for (uint32_t j = 0; j < i; j++)
            {
                if (selected[j] == s) 
                { 
                    already = true; 
                    break; 
                }
            }

            if (already) 
                continue;

            if (histogram[s] > max_count)
            {
                max_count = histogram[s];
                max_index = s;
            }
        }

        if (max_index == UINT32_MAX || max_count == 0)
            break;

        model->hot_values[i] = (uint8_t)max_index;
        selected[i] = max_index;
        model->num_hot_values++;
    }

    model->k = compute_best_k(histogram, model->hot_values, model->num_hot_values);

    switch(model->k)
    {
    case 0 : model->num_hot_values = le_min(model->num_hot_values, 7); break;
    case 1 : model->num_hot_values = le_min(model->num_hot_values, 12); break;
    case 2 : model->num_hot_values = le_min(model->num_hot_values, 20); break;
    case 3 : model->num_hot_values = le_min(model->num_hot_values, 32); break;
    case 4 : model->num_hot_values = le_min(model->num_hot_values, 48); break;
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void rice_encode(le_stream *s, uint32_t value, uint8_t k) 
{
    uint32_t q = value >> k;
    uint32_t r = value & ((1U << k) - 1U);

    // unary
    if (q > 0) le_write_bits(s, (uint8_t)((1U << q) - 1U), (uint8_t)q);
    le_write_bits(s, 0, 1);

    // remainder
    if (k > 0) le_write_bits(s, (uint8_t)r, k);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_encode(le_stream *s, le_model *model, uint8_t value)
{
#ifdef LE_CHECKS
    assert(s->mode == le_mode_encode);
#endif

    for(uint32_t i=0; i<model->num_hot_values; ++i)
    {
        if (value == model->hot_values[i])
        {
            le_write_bits(s, 0, 1);
            rice_encode(s, i, model->k);
            return;
        }
    }

    le_write_bits(s, 1, 1);
    le_write_byte(s, value);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_decode(le_stream *restrict s, const le_model *restrict model) 
{
    if (s->bits_available < 16)
        le_refill(s);

    // escape flag
    uint32_t flag = (uint32_t)(s->bit_reservoir & 1U);
    s->bit_reservoir >>= 1;
    s->bits_available -= 1;

    if (flag == 1)
    {
        uint32_t raw_val = (uint32_t)(s->bit_reservoir & 0xFFU);
        s->bit_reservoir >>= 8;
        s->bits_available -= 8;
        return (uint8_t)raw_val;
    }

    // rice decoding
    uint32_t q = 0;
    while ((s->bit_reservoir & (1ULL << q)) != 0)
        q++;

    // unary
    s->bit_reservoir >>= (q + 1);
    s->bits_available -= (q + 1);

    // remainder
    uint32_t r = 0;
    uint32_t k = model->k;
    if (k > 0)
    {
        r = (uint32_t)(s->bit_reservoir & ((1ULL << k) - 1U));
        s->bit_reservoir >>= k;
        s->bits_available -= k;
    }

    uint32_t index = (q << k) | r;

#ifdef LE_CHEKCS
    assert(index < model->num_hot_values);
#endif
    return model->hot_values[index];
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_model_save(le_stream *s, const le_model *model)
{
    le_write_byte(s, model->k);
    le_write_byte(s, model->num_hot_values);
    for(uint32_t i=0; i<model->num_hot_values; ++i)
        le_write_byte(s, model->hot_values[i]);
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_model_load(le_stream *s, le_model *model)
{
    model->k = le_read_byte(s);
    model->num_hot_values = le_read_byte(s);

    for(uint32_t i=0; i<model->num_hot_values; ++i)
        model->hot_values[i] = le_read_byte(s);
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
static inline void le_encode_delta(le_stream *s, int8_t delta)
{
    uint8_t zz = zigzag8_encode(delta);

    // hardcoded k=2
    if (zz < 20)
    {
        le_write_bits(s, 0, 1);
        rice_encode(s, zz, 2);
    }
    else
    {
        le_write_bits(s, 1, 1);
        le_write_byte(s, (uint8_t)delta);
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline int8_t le_decode_delta(le_stream* s)
{
    if (s->bits_available < 16) le_refill(s);

    uint32_t flag = (uint32_t)(s->bit_reservoir & 1U);
    s->bit_reservoir >>= 1;
    s->bits_available -= 1;

    // escape
    if (flag == 1)
    {
        uint8_t raw_val = (uint8_t)(s->bit_reservoir & 0xFFU);
        s->bit_reservoir >>= 8;
        s->bits_available -= 8;
        return (int8_t)raw_val;
    }

    uint32_t q = 0;
    while ((s->bit_reservoir & (1ULL << q)) != 0) 
        q++;
    
    s->bit_reservoir >>= (q + 1);
    s->bits_available -= (q + 1);

    uint32_t r = (uint32_t)(s->bit_reservoir & 3U); // k=2, so mask 0b11
    s->bit_reservoir >>= 2;
    s->bits_available -= 2;

    uint32_t zz = (q << 2) | r;
    return zigzag8_decode((uint8_t)zz);
}

#endif

