#include "greatest.h"
#include "../lite_encoding.h"


TEST stream_write(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_begin_encode(&stream);
    le_write_byte(&stream, 134);
    le_write_dibit(&stream, 3);
    le_write_nibble(&stream, 15);
    le_write_nibble(&stream, 1);
    le_write_byte(&stream, 56);

    ASSERT_EQ(4, le_end_encode(&stream));

    PASS();
}

TEST stream_read(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_begin_encode(&stream);
    le_write_byte(&stream, 134);
    le_write_dibit(&stream, 3);
    le_write_nibble(&stream, 15);
    le_write_nibble(&stream, 1);
    le_write_byte(&stream, 56);

    ASSERT_EQ(4, le_end_encode(&stream));

    le_begin_decode(&stream);

    ASSERT_EQ(134, le_read_byte(&stream));
    ASSERT_EQ(3, le_read_dibit(&stream));
    ASSERT_EQ(15, le_read_nibble(&stream));
    ASSERT_EQ(1, le_read_nibble(&stream));
    ASSERT_EQ(56, le_read_byte(&stream));

    le_end_decode(&stream);

    PASS();
}

TEST nibble14(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    const uint8_t sequence[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,1,2,3,4,5,6,7,8,9,10,11,12,13,14};

    le_histogram histogram;
    histogram_init(&histogram, 256);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        histogram.count[sequence[i]]++;

    le_model model;
    le_model_init(&model, histogram.count, histogram.num_symbols);

    le_begin_encode(&stream);
    le_model_save(&stream, &model);

    ASSERT_EQ(15, model.cold_min);
    ASSERT_EQ(19, model.cold_max);
    ASSERT_EQ(4, model.cold_num_bits);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        le_encode_byte(&stream, &model, sequence[i]);

    le_end_encode(&stream);

    le_begin_decode(&stream);

    le_model new_model;
    le_model_load(&stream, &new_model);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        ASSERT_EQ(sequence[i], le_decode_byte(&stream, &new_model));

    le_end_decode(&stream);


    PASS();
}

TEST no_compression(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    const uint8_t sequence[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40};

    le_histogram histogram;
    histogram_init(&histogram, 256);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        histogram.count[sequence[i]]++;

    le_model model;
    le_model_init(&model, histogram.count, histogram.num_symbols);

    ASSERT_EQ(true, model.no_compression);

    le_begin_encode(&stream);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        le_encode_byte(&stream, &model, sequence[i]);

    ASSERT_EQ(sizeof(sequence), le_end_encode(&stream));

    le_begin_decode(&stream);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        ASSERT_EQ(sequence[i], le_decode_byte(&stream, &model));

    le_end_decode(&stream);

    PASS();
}

TEST delta(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_begin_encode(&stream);
        le_encode_delta(&stream, -1);
        ASSERT_EQ(0, stream.position);
        le_encode_delta(&stream, -3);
        ASSERT_EQ(0, stream.position);
        le_encode_delta(&stream, 0);
        ASSERT_EQ(1, stream.position);
        le_encode_delta(&stream, 127);
        ASSERT_EQ(3, stream.position);
    le_end_encode(&stream);

    le_begin_decode(&stream);
        ASSERT_EQ(-1, le_decode_delta(&stream));
        ASSERT_EQ(-3, le_decode_delta(&stream));
        ASSERT_EQ(0, le_decode_delta(&stream));
        ASSERT_EQ(127, le_decode_delta(&stream));
    le_end_decode(&stream);

    PASS();
}


GREATEST_MAIN_DEFS();

int main(void) 
{
    GREATEST_INIT();
    
    RUN_TEST(stream_write);
    RUN_TEST(stream_read);
    RUN_TEST(nibble14);
    RUN_TEST(no_compression);
    RUN_TEST(delta);

    GREATEST_MAIN_END();
}

