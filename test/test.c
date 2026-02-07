#include "greatest.h"
#include "../lite_encoding.h"


TEST codec(void)
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
    le_model_init(&model, histogram.count);

    le_begin_encode(&stream);
    le_model_save(&stream, &model);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        le_encode(&stream, &model, sequence[i]);

    printf("model stats\n\tk=%u\n\tnum_hot_values=%u\n", model.k, model.num_hot_values);
    printf("compressed size : %zu vs original size : %zu\n", le_end_encode(&stream) - 2 - model.num_hot_values, sizeof(sequence));

    le_begin_decode(&stream);

    le_model new_model;
    le_model_load(&stream, &new_model);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        ASSERT_EQ(sequence[i], le_decode(&stream, &new_model));

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
        le_encode_delta(&stream, -3);
        le_encode_delta(&stream, 0);
        le_encode_delta(&stream, 127);
    printf("compressed size : %zu vs original size : %u\n", le_end_encode(&stream), 4U);

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
    
    RUN_TEST(codec);
    RUN_TEST(delta);

    GREATEST_MAIN_END();
}

