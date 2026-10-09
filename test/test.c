#include "greatest.h"
#include "../lite_encoding.h"
#include "default_font_atlas.h"

/* Deterministic PRNG for repeatable test data */
static uint32_t s_rng_state = 0x12345678U;
static uint32_t xorshift32(void)
{
    uint32_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return x;
}

/*
 * Assert that two models agree on all fields (encoder/decoder must stay in
 * sync). Must be a macro because Greatest's ASSERT_* macros return from the
 * enclosing function, which a void helper cannot do.
 */
#define ASSERT_MODELS_SYNCED(a, b)                                            \
    do {                                                                      \
        ASSERT_EQ((a)->k, (b)->k);                                            \
        ASSERT_EQ((int)(a)->k_trend, (int)(b)->k_trend);                      \
        ASSERT_EQ((int)(a)->num_symbols, (int)(b)->num_symbols);              \
        ASSERT_EQ((a)->is_static, (b)->is_static);                            \
        ASSERT_EQ((int)sizeof((a)->alphabet), (int)sizeof((b)->alphabet));    \
        ASSERT_EQ(memcmp((a)->alphabet, (b)->alphabet, LE_ALPHABET_SIZE), 0); \
        ASSERT_EQ(memcmp((a)->index, (b)->index, LE_ALPHABET_SIZE), 0);       \
    } while (0)

TEST symbols(void)
{
    uint8_t buffer[32768];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_dynamic_model_init(&model);

    le_begin_encode(&stream);

    for(uint32_t i=0; i<default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model, default_font_atlas[i]);

    printf("compressed size : %zu vs original size : %zu\n", le_end_encode(&stream), default_font_atlas_size);

    le_begin_decode(&stream);

    le_model new_model;
    le_dynamic_model_init(&new_model);

    for(uint32_t i=0; i<default_font_atlas_size; ++i)
        ASSERT_EQ(default_font_atlas[i], le_decode_symbol(&stream, &new_model));

    le_end_decode(&stream);


    PASS();
}

TEST symbols_static(void)
{
    uint8_t buffer[32768];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    uint32_t histogram[LE_ALPHABET_SIZE] = {0};
    for (size_t i = 0; i < default_font_atlas_size; ++i)
    {
        histogram[default_font_atlas[i]]++;
    }

    le_model model;
    le_static_model_init(&model, histogram, LE_ALPHABET_SIZE);
    uint8_t initial_k = model.k;

    // 3. Encode symbols
    le_begin_encode(&stream);

    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model, default_font_atlas[i]);

    printf("compressed size (static) : %zu vs original size : %zu\n", le_end_encode(&stream), default_font_atlas_size);

    le_begin_decode(&stream);

    le_model new_model;
    le_static_model_load(&new_model, model.alphabet, LE_ALPHABET_SIZE, initial_k, model.q_escape);

    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        ASSERT_EQ(default_font_atlas[i], le_decode_symbol(&stream, &new_model));

    le_end_decode(&stream);

    PASS();
}

TEST delta(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_dynamic_model_init(&model);

    le_begin_encode(&stream);
        le_encode_delta(&stream, &model, -1);
        le_encode_delta(&stream, &model, -3);
        le_encode_delta(&stream, &model, 0);
        le_encode_delta(&stream, &model, 10);
    printf("compressed size : %zu vs original size : %u\n", le_end_encode(&stream), 4U);

    le_model new_model;
    le_dynamic_model_init(&new_model);

    le_begin_decode(&stream);
        ASSERT_EQ(-1, le_decode_delta(&stream, &new_model));
        ASSERT_EQ(-3, le_decode_delta(&stream, &new_model));
        ASSERT_EQ(0, le_decode_delta(&stream, &new_model));
        ASSERT_EQ(10, le_decode_delta(&stream, &new_model));
    le_end_decode(&stream);

    PASS();
}

TEST overrun(void)
{
    uint8_t buffer[245];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_dynamic_model_init(&model);

    le_begin_encode(&stream);

    for(uint32_t i=0; i<default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model, default_font_atlas[i]);

    ASSERT_NEQ(stream.status, LE_OK);
    ASSERT_EQ(le_end_encode(&stream), 0);

    le_begin_decode(&stream);

    le_model new_model;
    le_dynamic_model_init(&new_model);

    for(uint32_t i=0; i<default_font_atlas_size; ++i)
        le_decode_symbol(&stream, &new_model);

    ASSERT_NEQ(stream.status, LE_OK);

    le_end_decode(&stream);


    PASS();
}


/*
 * Rice coder: roundtrip every 8-bit value for k = 0..8, paired with the
 * matching q_escape_for_k table entry (k=8 is the largest value whose table
 * entry exists and whose shifts are defined). For k <= 6 the escape threshold
 * is 4, so most values take the raw-byte escape path; for k = 7..8 it is 255
 * and no escape fires. Values stay <= 255 so no undefined behavior is possible.
 */
TEST rice_all_values(void)
{
    static uint8_t buffer[1024];

    for (uint8_t k = 0; k <= 8; ++k)
    {
        le_stream stream;
        le_init(&stream, buffer, sizeof(buffer));
        le_begin_encode(&stream);

        for (uint32_t v = 0; v < 256; ++v)
            rice_encode(&stream, v, k, q_escape_for_k[k]);

        ASSERT_EQ(stream.status, LE_OK);
        size_t size = le_end_encode(&stream);
        ASSERT_EQ(stream.status, LE_OK);
        ASSERT_NEQ((int)size, 0); // 256 rice-coded values always produce some bytes

        le_init(&stream, buffer, sizeof(buffer));
        le_begin_decode(&stream);

        for (uint32_t v = 0; v < 256; ++v)
        {
            uint8_t got = rice_decode(&stream, k, q_escape_for_k[k]);
            ASSERT_EQ((int)got, (int)v);
            ASSERT_EQ(stream.status, LE_OK);
        }

        le_end_decode(&stream);
    }

    PASS();
}

/*
 * Dynamic symbol coder over several data shapes. The decoder must reproduce
 * every symbol, and once the full stream is consumed the encoder and decoder
 * models must be bit-identical (alphabet, index, k, k_trend, num_symbols).
 *
 * Note: model equality can only be checked at the *end* of the stream: the
 * encoder model has already adapted over the whole stream while the decoder
 * model only adapts up to where it has decoded.
 */
TEST symbols_patterns(void)
{
    static uint8_t buffer[32768];
    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model enc, dec;
    le_dynamic_model_init(&enc);
    le_dynamic_model_init(&dec);

    le_begin_encode(&stream);

    for (uint32_t i = 0; i < 3000; ++i) le_encode_symbol(&stream, &enc, 0);        // all zero
    ASSERT_EQ(stream.status, LE_OK);
    for (uint32_t i = 0; i < 2000; ++i) le_encode_symbol(&stream, &enc, 0xFF);     // all 0xFF
    ASSERT_EQ(stream.status, LE_OK);
    for (uint32_t i = 0; i < 256; ++i)  le_encode_symbol(&stream, &enc, (uint8_t)i); // ascending ramp
    for (uint32_t i = 0; i < 1024; ++i) le_encode_symbol(&stream, &enc, (uint8_t)(i & 0xFF)); // full cycle
    for (uint32_t i = 0; i < 1000; ++i) le_encode_symbol(&stream, &enc, (uint8_t)(i % 7));  // repetitive
    s_rng_state = 0x12345678U; // re-seed: decode side starts from the same state
    for (uint32_t i = 0; i < 2000; ++i) le_encode_symbol(&stream, &enc, (uint8_t)(xorshift32() & 0xFF)); // uniform random

    ASSERT_EQ(stream.status, LE_OK);
    printf("patterns compressed size : %zu vs original size : %u\n", le_end_encode(&stream), 9282U);
    ASSERT_EQ(stream.status, LE_OK);

    le_init(&stream, buffer, sizeof(buffer));
    le_begin_decode(&stream);

    for (uint32_t i = 0; i < 3000; ++i) ASSERT_EQ(0, le_decode_symbol(&stream, &dec));
    for (uint32_t i = 0; i < 2000; ++i) ASSERT_EQ(0xFF, le_decode_symbol(&stream, &dec));
    for (uint32_t i = 0; i < 256; ++i)  ASSERT_EQ((uint8_t)i, le_decode_symbol(&stream, &dec));
    for (uint32_t i = 0; i < 1024; ++i) ASSERT_EQ((uint8_t)(i & 0xFF), le_decode_symbol(&stream, &dec));
    for (uint32_t i = 0; i < 1000; ++i) ASSERT_EQ((uint8_t)(i % 7), le_decode_symbol(&stream, &dec));
    s_rng_state = 0x12345678U;
    for (uint32_t i = 0; i < 2000; ++i) ASSERT_EQ((uint8_t)(xorshift32() & 0xFF), le_decode_symbol(&stream, &dec));
    // Both models saw the same 9282 indices in the same order, so they must match
    ASSERT_MODELS_SYNCED(&enc, &dec);
    ASSERT_EQ(stream.status, LE_OK);

    le_end_decode(&stream);

    PASS();
}

/* Literals over boundary values, a full ramp, cycles, and random data. */
TEST literals_patterns(void)
{
    static uint8_t buffer[32768];
    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model enc, dec;
    le_dynamic_model_init(&enc);
    le_dynamic_model_init(&dec);

    le_begin_encode(&stream);

    const uint8_t boundary[] = {0, 1, 2, 127, 128, 254, 255};
    for (size_t i = 0; i < sizeof(boundary); ++i) le_encode_literal(&stream, &enc, boundary[i]);
    for (uint32_t i = 0; i < 256; ++i) le_encode_literal(&stream, &enc, (uint8_t)i);
    for (uint32_t i = 0; i < 1024; ++i) le_encode_literal(&stream, &enc, (uint8_t)(i & 0xFF));
    s_rng_state = 0x12345678U; // re-seed: decode side starts from the same state
    for (uint32_t i = 0; i < 2000; ++i) le_encode_literal(&stream, &enc, (uint8_t)(xorshift32() & 0xFF));

    ASSERT_EQ(stream.status, LE_OK);
    ASSERT_NEQ(le_end_encode(&stream), 0);
    ASSERT_EQ(stream.status, LE_OK);

    le_init(&stream, buffer, sizeof(buffer));
    le_begin_decode(&stream);

    for (size_t i = 0; i < sizeof(boundary); ++i) ASSERT_EQ(boundary[i], le_decode_literal(&stream, &dec));
    for (uint32_t i = 0; i < 256; ++i) ASSERT_EQ((uint8_t)i, le_decode_literal(&stream, &dec));
    for (uint32_t i = 0; i < 1024; ++i) ASSERT_EQ((uint8_t)(i & 0xFF), le_decode_literal(&stream, &dec));
    s_rng_state = 0x12345678U;
    for (uint32_t i = 0; i < 2000; ++i) ASSERT_EQ((uint8_t)(xorshift32() & 0xFF), le_decode_literal(&stream, &dec));
    // Both models updated k on the same values in the same order, so they must match
    ASSERT_MODELS_SYNCED(&enc, &dec);
    ASSERT_EQ(stream.status, LE_OK);

    le_end_decode(&stream);

    PASS();
}

/*
 * Deltas over every int8 value (covers the full zigzag range -128..127),
 * plus a zigzag8 roundtrip check and random data.
 */
TEST deltas_full_range(void)
{
    static uint8_t buffer[32768];
    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model enc, dec;
    le_dynamic_model_init(&enc);
    le_dynamic_model_init(&dec);

    le_begin_encode(&stream);

    for (int v = -128; v <= 127; ++v) le_encode_delta(&stream, &enc, (int8_t)v);
    s_rng_state = 0x12345678U; // re-seed: decode side starts from the same state
    for (int i = 0; i < 1000; ++i)    le_encode_delta(&stream, &enc, (int8_t)(xorshift32() % 21 - 10));

    ASSERT_EQ(stream.status, LE_OK);
    ASSERT_NEQ(le_end_encode(&stream), 0);
    ASSERT_EQ(stream.status, LE_OK);

    le_init(&stream, buffer, sizeof(buffer));
    le_begin_decode(&stream);

    for (int v = -128; v <= 127; ++v)
        ASSERT_EQ(v, le_decode_delta(&stream, &dec));
    s_rng_state = 0x12345678U;
    for (int i = 0; i < 1000; ++i)
        ASSERT_EQ((int)(int8_t)(xorshift32() % 21 - 10), le_decode_delta(&stream, &dec));
    // Both models updated k on the same zigzag values in the same order, so they must match
    ASSERT_MODELS_SYNCED(&enc, &dec);
    ASSERT_EQ(stream.status, LE_OK);

    le_end_decode(&stream);

    // zigzag8 is its own inverse for every value
    for (int v = -128; v <= 127; ++v)
        ASSERT_EQ(v, zigzag8_decode(zigzag8_encode((int8_t)v)));

    PASS();
}

/* Raw bit/byte I/O within the documented 1..8 bit range. */
TEST bits_bytes_roundtrip(void)
{
    static uint8_t buffer[256];

    for (uint8_t n = 1; n <= 8; ++n)
    {
        le_stream stream;
        le_init(&stream, buffer, sizeof(buffer));
        le_begin_encode(&stream);

        s_rng_state = 0x12345678U; // re-seed: decode side starts from the same state
        for (uint32_t v = 0; v < 100; ++v)
        {
            uint8_t bits = (uint8_t)(xorshift32() & ((1U << n) - 1U));
            le_write_bits(&stream, bits, n);
        }

        ASSERT_EQ(stream.status, LE_OK);
        ASSERT_NEQ(le_end_encode(&stream), 0);
        ASSERT_EQ(stream.status, LE_OK);

        le_init(&stream, buffer, sizeof(buffer));
        le_begin_decode(&stream);

        s_rng_state = 0x12345678U;
        for (uint32_t v = 0; v < 100; ++v)
            ASSERT_EQ((int)(xorshift32() & ((1U << n) - 1U)), (int)le_read_bits(&stream, n));
        ASSERT_EQ(stream.status, LE_OK);

        le_end_decode(&stream);
    }

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));
    le_begin_encode(&stream);

    s_rng_state = 0x12345678U; // re-seed: decode side starts from the same state
    for (uint32_t v = 0; v < 100; ++v)
        le_write_byte(&stream, (uint8_t)(xorshift32() & 0xFF));

    ASSERT_EQ(stream.status, LE_OK);
    ASSERT_NEQ(le_end_encode(&stream), 0);
    ASSERT_EQ(stream.status, LE_OK);

    le_init(&stream, buffer, sizeof(buffer));
    le_begin_decode(&stream);

    s_rng_state = 0x12345678U;
    for (uint32_t v = 0; v < 100; ++v)
        ASSERT_EQ((int)(xorshift32() & 0xFF), (int)le_read_byte(&stream));
    ASSERT_EQ(stream.status, LE_OK);

    le_end_decode(&stream);

    PASS();
}

/* Decoding from a buffer one byte too small must flag LE_BUFFER_OVERRUN. */
TEST decode_overrun(void)
{
    static uint8_t buffer[32768];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_dynamic_model_init(&model);

    le_begin_encode(&stream);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model, default_font_atlas[i]);

    size_t size = le_end_encode(&stream);
    ASSERT_NEQ((int)size, 0);
    ASSERT_EQ(stream.status, LE_OK);
    ASSERT_NEQ((int)size, 1); // leaving at least one byte of data to consume

    le_init(&stream, buffer, size - 1);
    le_begin_decode(&stream);

    le_model new_model;
    le_dynamic_model_init(&new_model);

    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_decode_symbol(&stream, &new_model);

    ASSERT_EQ(stream.status, LE_BUFFER_OVERRUN);

    le_end_decode(&stream);

    PASS();
}

/* Buffers sized exactly to the compressed output work; one byte short doesn't. */
TEST exact_size_buffers(void)
{
    static uint8_t buffer[32768];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_dynamic_model_init(&model);

    le_begin_encode(&stream);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model, default_font_atlas[i]);

    size_t size = le_end_encode(&stream);
    ASSERT_NEQ((int)size, 0);
    ASSERT_EQ(stream.status, LE_OK);

    // Re-encode into a buffer that is exactly big enough. A fresh model is
    // required: the first encode left `model` adapted (different k), which
    // would produce a different (still valid) compressed size.
    le_model fresh;
    le_dynamic_model_init(&fresh);
    le_init(&stream, buffer, size);
    le_begin_encode(&stream);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &fresh, default_font_atlas[i]);
    ASSERT_EQ((int)le_end_encode(&stream), (int)size);
    ASSERT_EQ(stream.status, LE_OK);

    // Decode from the exactly-sized buffer succeeds
    le_init(&stream, buffer, size);
    le_begin_decode(&stream);
    le_model new_model;
    le_dynamic_model_init(&new_model);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        ASSERT_EQ(default_font_atlas[i], le_decode_symbol(&stream, &new_model));
    ASSERT_EQ(stream.status, LE_OK);
    le_end_decode(&stream);

    // Decode from one byte too little fails
    le_init(&stream, buffer, size - 1);
    le_begin_decode(&stream);
    le_model new_model2;
    le_dynamic_model_init(&new_model2);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_decode_symbol(&stream, &new_model2);
    ASSERT_EQ(stream.status, LE_BUFFER_OVERRUN);
    le_end_decode(&stream);

    PASS();
}

/* Static model: sparse histogram, stable under encoding, roundtrip via load. */
TEST static_model(void)
{
    static uint8_t buffer[32768];

    uint32_t histogram[LE_ALPHABET_SIZE] = {0};
    histogram[0] = 1000;
    for (uint32_t i = 1; i < 16; ++i)
        histogram[i] = 1;

    le_model model;
    le_static_model_init(&model, histogram, LE_ALPHABET_SIZE);

    ASSERT_EQ((int)model.num_symbols, 16);
    ASSERT_EQ((int)model.k, (int)model.k);
    ASSERT_EQ(model.k < 8, 1);
    ASSERT_EQ(model.is_static, 1);
    for (uint32_t i = 0; i < 16; ++i)
        ASSERT_EQ((int)model.index[i], (int)i); // 1000/1/1/... sorts 0..15 first

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    const uint8_t initial_k = model.k;
    const int8_t initial_trend = model.k_trend;
    uint8_t saved_alphabet[LE_ALPHABET_SIZE];
    memcpy(saved_alphabet, model.alphabet, LE_ALPHABET_SIZE);

    le_begin_encode(&stream);
    for (uint32_t i = 0; i < 512; ++i)
        le_encode_symbol(&stream, &model, saved_alphabet[i % 16]);
    ASSERT_EQ(stream.status, LE_OK);
    ASSERT_NEQ(le_end_encode(&stream), 0);

    // A static model must not adapt during encoding
    ASSERT_EQ(model.k, initial_k);
    ASSERT_EQ((int)model.k_trend, (int)initial_trend);
    ASSERT_EQ((int)model.num_symbols, 16);
    ASSERT_EQ(memcmp(model.alphabet, saved_alphabet, LE_ALPHABET_SIZE), 0);

    le_init(&stream, buffer, sizeof(buffer));
    le_begin_decode(&stream);

    le_model dec_model;
    le_static_model_load(&dec_model, saved_alphabet, 16, initial_k, model.q_escape);

    for (uint32_t i = 0; i < 512; ++i)
        ASSERT_EQ(saved_alphabet[i % 16], le_decode_symbol(&stream, &dec_model));
    ASSERT_EQ(stream.status, LE_OK);

    le_end_decode(&stream);

    PASS();
}

/* A stream can be re-initialized onto a smaller buffer and still roundtrip. */
TEST stream_reuse(void)
{
    static uint8_t buffer[32768];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_dynamic_model_init(&model);

    le_begin_encode(&stream);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model, default_font_atlas[i]);

    size_t size = le_end_encode(&stream);
    ASSERT_NEQ((int)size, 0);
    ASSERT_EQ(stream.status, LE_OK);

    // Re-init the same stream onto a much smaller buffer and redo the roundtrip
    le_init(&stream, buffer, size);
    le_begin_encode(&stream);
    le_model model2;
    le_dynamic_model_init(&model2);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model2, default_font_atlas[i]);
    ASSERT_EQ((int)le_end_encode(&stream), (int)size);
    ASSERT_EQ(stream.status, LE_OK);

    le_init(&stream, buffer, size);
    le_begin_decode(&stream);
    le_model dec_model;
    le_dynamic_model_init(&dec_model);
    for (uint32_t i = 0; i < default_font_atlas_size; ++i)
        ASSERT_EQ(default_font_atlas[i], le_decode_symbol(&stream, &dec_model));
    ASSERT_EQ(stream.status, LE_OK);

    le_end_decode(&stream);

    PASS();
}


GREATEST_MAIN_DEFS();

int main(void) 
{
    GREATEST_INIT();
    
    RUN_TEST(symbols);
    RUN_TEST(symbols_static);
    RUN_TEST(delta);
    RUN_TEST(overrun);
    RUN_TEST(rice_all_values);
    RUN_TEST(symbols_patterns);
    RUN_TEST(literals_patterns);
    RUN_TEST(deltas_full_range);
    RUN_TEST(bits_bytes_roundtrip);
    RUN_TEST(decode_overrun);
    RUN_TEST(exact_size_buffers);
    RUN_TEST(static_model);
    RUN_TEST(stream_reuse);

    GREATEST_MAIN_END();
}

