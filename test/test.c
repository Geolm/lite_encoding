#include "greatest.h"
#include "../lite_encoding.h"
#include "default_font_atlas.h"


TEST symbols(void)
{
    uint8_t buffer[32768];

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_model_init(&model);

    le_begin_encode(&stream);

    for(uint32_t i=0; i<default_font_atlas_size; ++i)
        le_encode_symbol(&stream, &model, default_font_atlas[i]);

    printf("compressed size : %zu vs original size : %zu\n", le_end_encode(&stream), default_font_atlas_size);

    le_begin_decode(&stream);

    le_model new_model;
    le_model_init(&new_model);

    for(uint32_t i=0; i<default_font_atlas_size; ++i)
        ASSERT_EQ(default_font_atlas[i], le_decode_symbol(&stream, &new_model));

    le_end_decode(&stream);


    PASS();
}

TEST delta(void)
{
    uint8_t buffer[2048];

    static const uint8_t sequence[] = {1, 65, 5, 3, 7, 39, 4, 90, 10, 65, 5, 3, 1, 40, 39, 40, 6, 5, 3, 7, 3, 2, 1, 5, 90, 65,
                                       1, 65, 5, 3, 7, 39, 4, 91, 10, 65, 5, 3, 1, 40, 39, 40, 6, 5, 3, 7, 3, 2, 1, 5, 90, 65,
                                       1, 65, 5, 3, 4, 38, 4, 90, 10, 65, 5, 3, 1, 27, 39, 40, 6, 5, 3, 73, 3, 24, 1, 5, 90, 65,
                                       1, 65, 5, 3, 6, 39, 4, 90, 10, 65, 5, 3, 1, 40, 39, 40, 6, 5, 3, 7, 32, 2, 12, 5, 90, 65};

    le_stream stream;
    le_init(&stream, buffer, sizeof(buffer));

    le_model model;
    le_model_init(&model);

    le_begin_encode(&stream);
    
    for(uint32_t i=0; i<sizeof(sequence); ++i)
        le_encode_rle(&stream, &model, sequence[i]);
    printf("compressed size : %zu vs original size : %zu\n", le_end_encode(&stream), sizeof(sequence));

    le_model new_model;
    le_model_init(&new_model);

    le_begin_decode(&stream);
    for(uint32_t i=0; i<sizeof(sequence); ++i)
        ASSERT_EQ(sequence[i], le_decode_rle(&stream, &new_model));
    le_end_decode(&stream);

    PASS();
}

TEST rle(void)
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
    
    RUN_TEST(symbols);
    RUN_TEST(delta);
    RUN_TEST(rle);

    GREATEST_MAIN_END();
}

