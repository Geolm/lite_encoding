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


typedef struct le_stream
{
    uint8_t* buffer;
    uint8_t bit;
    size_t position;
    size_t size;
} le_stream;


static void le_init(le_stream *s, void* buffer, size_t size);
static void le_begin_encode(le_stream* s);
static size_t le_end_encode(le_stream* s);
static void le_begin_decode(le_stream* s);
static void le_end_decode(le_stream* s);


static void le_write_byte(le_stream *s, uint8_t value);
static void le_write_nibble(le_stream *s, uint8_t nibble);
static void le_write_dibit(le_stream *s, uint8_t dibit);

static uint8_t le_read_byte(le_stream *s);
static uint8_t le_read_nibble(le_stream *s);
static uint8_t le_read_dibit(le_stream *s);

// Nibble-14 Encoding Model
//
// The Nibble-14 model compresses data by focusing on the 14 most frequent byte values in the input:
// The model stores the 14 hot values derived from the input histogram.
// Each byte is encoded as a 4-bit nibble:
//      0–13 → index into the hot values.
//      14 → run-length code (RLE) for repeating the previous byte.
//      15 → escape code, followed by the full byte if it doesn’t match the hot values or RLE.
//
// This approach efficiently compresses data with skewed distributions and repeated bytes while keeping the stream compact.

typedef struct le_model
{
    uint8_t hot_values[14];
    uint8_t last_value;
    bool no_compression;
} le_model;


void le_model_init(le_model *model, const uint32_t *histogram, uint32_t num_symbols);
void le_encode_byte(le_stream *s, le_model *model, uint8_t value);
uint8_t le_decode_byte(le_stream *s, le_model *model);


// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_init(le_stream *s, void* buffer, size_t size)
{
    assert(s != NULL);
    assert(buffer != NULL);

    s->buffer = (uint8_t*) buffer;
    s->size = size;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_begin_encode(le_stream* s)
{
    memset(s->buffer, 0, s->size);
    s->position = 0;
    s->bit = 0;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline size_t le_end_encode(le_stream *s)
{
    assert(s != NULL);

    size_t final_size = s->position;

    if (s->bit > 0)
        final_size++;

    return final_size;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_begin_decode(le_stream* s)
{
    s->position = 0;
    s->bit = 0;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_end_decode(le_stream* s)
{
    (void) s;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_write_nibble(le_stream *s, uint8_t nibble)
{
    assert(nibble <= 0x0F);
    assert(s->position < s->size);

    switch (s->bit)
    {
        case 0:
        {
            s->buffer[s->position] |= nibble << 4;
            s->bit = 4;
            break;
        }

        case 2:
        {
            s->buffer[s->position] |= nibble << 2;
            s->bit = 6;
            break;
        }

        case 4:
        {
            s->buffer[s->position] |= nibble;
            s->bit = 0;
            s->position++;
            break;
        }

        case 6:
        {
            assert(s->position + 1 < s->size);

            s->buffer[s->position]     |= nibble >> 2;
            s->buffer[s->position + 1] |= nibble << 6;
            s->bit = 2;
            s->position++;
            break;
        }

        default:
            assert(0);
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_read_nibble(le_stream *s)
{
    assert(s->position < s->size);

    uint8_t nibble;

    switch (s->bit)
    {
        case 0:
        {
            nibble = s->buffer[s->position] >> 4;
            s->bit = 4;
            break;
        }

        case 2:
        {
            nibble = (s->buffer[s->position] >> 2) & 0x0F;
            s->bit = 6;
            break;
        }

        case 4:
        {
            nibble = s->buffer[s->position] & 0x0F;
            s->bit = 0;
            s->position++;
            break;
        }

        case 6:
        {
            assert(s->position + 1 < s->size);

            uint8_t hi = (s->buffer[s->position] & 0x03) << 2;
            uint8_t lo =  s->buffer[s->position + 1] >> 6;
            nibble = hi | lo;

            s->bit = 2;
            s->position++;
            break;
        }

        default:
            assert(0);
    }

    return nibble;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_write_dibit(le_stream *s, uint8_t dibit)
{
    assert(dibit <= 0x03);
    assert(s->position < s->size);

    uint8_t shift = 6 - s->bit;
    s->buffer[s->position] |= (dibit << shift);
    s->bit += 2;

    if (s->bit == 8)
    {
        s->bit = 0;
        s->position++;
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline void le_write_byte(le_stream *s, uint8_t value)
{
    if (s->bit == 0) 
    {
        assert(s->position < s->size);
        s->buffer[s->position++] = value;
    } 
    else 
    {
        // byte is split across two memory locations
        assert(s->position + 1 < s->size);
        
        uint8_t shift = s->bit;
        uint8_t remaining = 8 - shift;

        s->buffer[s->position]     |= (value >> shift);
        s->buffer[s->position + 1] |= (value << remaining);
        s->position++;
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_read_dibit(le_stream *s)
{
    assert(s->position < s->size);

    // Extract 2 bits based on the current bit offset (reading from MSB to LSB)
    uint8_t shift = 6 - s->bit;
    uint8_t dibit = (s->buffer[s->position] >> shift) & 0x03;

    s->bit += 2;

    if (s->bit == 8)
    {
        s->bit = 0;
        s->position++;
    }

    return dibit;
}

// ----------------------------------------------------------------------------------------------------------------------------
static inline uint8_t le_read_byte(le_stream *s)
{
    uint8_t value = 0;

    if (s->bit == 0)
    {
        // Aligned case
        assert(s->position < s->size);
        value = s->buffer[s->position++];
    }
    else
    {
        // Straddled case: value is split across s->buffer[pos] and s->buffer[pos+1]
        assert(s->position + 1 < s->size);

        uint8_t shift = s->bit;
        uint8_t remaining = 8 - shift;

        uint8_t hi = (s->buffer[s->position] << shift);
        uint8_t lo = (s->buffer[s->position + 1] >> remaining);
        
        value = hi | lo;
        s->position++;
        // s->bit remains unchanged (e.g., if it was 4, it's still 4 in the next byte)
    }

    return value;
}

#endif

