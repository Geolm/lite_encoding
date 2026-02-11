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

    GREATEST_MAIN_END();
}

