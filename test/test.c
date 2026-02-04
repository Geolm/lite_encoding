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


GREATEST_MAIN_DEFS();

int main(void) 
{
    GREATEST_INIT();
    
    RUN_TEST(stream_write);
    RUN_TEST(stream_read);

    GREATEST_MAIN_END();
}

