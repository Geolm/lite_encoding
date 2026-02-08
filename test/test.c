#include "greatest.h"
#include "../lite_encoding.h"


TEST codec(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    const uint8_t sequence[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,1,2,3,4,5,6,7,8,9,10,11,12,13,14};

    le_model model;
    le_model_init(&model);

    le_begin_encode(&stream);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        le_encode_symbol(&stream, &model, sequence[i]);

    printf("compressed size : %zu vs original size : %zu\n", le_end_encode(&stream), sizeof(sequence));

    le_begin_decode(&stream);

    le_model new_model;
    le_model_init(&new_model);

    for(uint32_t i=0; i<sizeof(sequence); ++i)
        ASSERT_EQ(sequence[i], le_decode_symbol(&stream, &new_model));

    le_end_decode(&stream);


    PASS();
}

TEST delta(void)
{
    uint8_t buffer[2048];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_model_init(&model);

    le_begin_encode(&stream);
        le_encode_delta(&stream, &model, -1);
        le_encode_delta(&stream, &model, -3);
        le_encode_delta(&stream, &model, 0);
        le_encode_delta(&stream, &model, 10);
    printf("compressed size : %zu vs original size : %u\n", le_end_encode(&stream), 4U);

    le_model new_model;
    le_model_init(&new_model);

    le_begin_decode(&stream);
        ASSERT_EQ(-1, le_decode_delta(&stream, &new_model));
        ASSERT_EQ(-3, le_decode_delta(&stream, &new_model));
        ASSERT_EQ(0, le_decode_delta(&stream, &new_model));
        ASSERT_EQ(10, le_decode_delta(&stream, &new_model));
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

