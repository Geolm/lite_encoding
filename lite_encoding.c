#include "lite_encoding.h"

#define MODEL_MAX_HOT (14)
#define MODEL_RLE (14)
#define MODEL_ESCAPE (15)

// ----------------------------------------------------------------------------------------------------------------------------
void le_model_init(le_model *model, const uint32_t *histogram, uint32_t num_symbols)
{
    memset(model->hot_values, 0, sizeof(model->hot_values));
    model->last_value = 0;
    model->no_compression = false;

    uint32_t selected[MODEL_MAX_HOT];
    for (uint32_t i = 0; i < MODEL_MAX_HOT; i++)
        selected[i] = UINT32_MAX;

    uint32_t hot_used = 0;

    // pick up to 14 most frequent symbols
    for (uint32_t i = 0; i < MODEL_MAX_HOT; i++)
    {
        uint32_t max_count = 0;
        uint32_t max_index = UINT32_MAX;

        for (uint32_t s = 0; s < num_symbols; s++)
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
        hot_used++;
    }

    // check if the compression will bring some gain
    // if the MODEL_MAX_HOT hot values are not enough present, escape code will make the compressed data bigger
    // a hot value hit uses 4 bits vs 12 bits for escape, basically we need more than 50% of the bytes to use
    // the hot values.

    uint64_t total_count = 0;
    for (uint32_t s = 0; s < num_symbols; s++)
        total_count += histogram[s];

    uint64_t hot_count = 0;
    for (uint32_t i = 0; i < hot_used; i++)
        hot_count += histogram[model->hot_values[i]];

    if (total_count > 0 && hot_count * 2 < total_count)
        model->no_compression = true;

    model->cold_min = UINT8_MAX;
    model->cold_max = 0;
    for(uint32_t s = 0; s < num_symbols; s++)
    {
        // skip hot symbols
        bool is_hot = false;
        for (uint32_t i = 0; i < hot_used; i++)
        {
            if (model->hot_values[i] == s)
            {
                is_hot = true;
                break;
            }
        }

        if (is_hot || histogram[s] == 0)
            continue;

        if (s < model->cold_min)
            model->cold_min = (uint8_t)s;
        if (s > model->cold_max)
            model->cold_max = (uint8_t)s;
    }

    if (model->cold_max >= model->cold_min)
    {
        uint8_t range = model->cold_max - model->cold_min;

        if (range >= 64)
            model->cold_num_bits = 8;
        else if (range >= 16)
            model->cold_num_bits = 6;
        else if (range >= 4)
            model->cold_num_bits = 4;
        else
            model->cold_num_bits = 2;
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
void le_encode_byte(le_stream *s, le_model *model, uint8_t value)
{
    assert(s->mode == le_mode_encode);

    // no compression
    if (model->no_compression)
    {
        le_write_byte(s, value);
    }
    else if (value == model->last_value)
    {
        // RLE
        le_write_nibble(s, MODEL_RLE);
    }
    else 
    {
        model->last_value = value;

        // hot values
        for (uint32_t i = 0; i < MODEL_MAX_HOT; i++)
        {
            if (model->hot_values[i] == value)
            {
                le_write_nibble(s, (uint8_t)i);
                return;
            }
        }

        // or escape code
        le_write_nibble(s, MODEL_ESCAPE);

        uint8_t residual = value - model->cold_min;

        switch(model->cold_num_bits)
        {
            case 2 : le_write_dibit(s, residual); break;
            case 4 : le_write_nibble(s, residual); break;
            case 6 : 
                {
                    le_write_dibit(s, residual >> 4);
                    le_write_nibble(s, residual&0xf);
                    break;
                }
            case 8 : le_write_byte(s, residual);break;
        }
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
uint8_t le_decode_byte(le_stream *s, le_model *model)
{
    if (model->no_compression)
        return le_read_byte(s);

    uint8_t nibble = le_read_nibble(s);

    if (nibble == MODEL_RLE)
        return model->last_value;

    uint8_t value;
    if (nibble == MODEL_ESCAPE)
    {
        switch(model->cold_num_bits)
        {
            case 2 : value = le_read_dibit(s); break;
            case 4 : value = le_read_nibble(s); break;
            case 6 : value = (le_read_dibit(s) << 4) | le_read_nibble(s); break;
            case 8 : value = le_read_byte(s);break;
        }
        value += model->cold_min;
    }
    else
        value = model->hot_values[nibble];

   
    model->last_value = value;
    return value;
}

// ----------------------------------------------------------------------------------------------------------------------------
void le_model_save(le_stream *s, const le_model *model)
{
    le_write_dibit(s, model->no_compression ? 1 : 0);
    if (!model->no_compression)
    {
        le_write_nibble(s, model->cold_num_bits);
        le_write_byte(s, model->cold_min);

        for(uint32_t i=0; i<MODEL_MAX_HOT; ++i)
            le_write_byte(s, model->hot_values[i]);
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
void le_model_load(le_stream *s, le_model *model)
{
    model->no_compression = (le_read_dibit(s) == 1);
    if (!model->no_compression)
    {
        model->cold_num_bits = le_read_nibble(s);
        model->cold_min = le_read_byte(s);
        model->last_value = 0;

        for(uint32_t i=0; i<MODEL_MAX_HOT; ++i)
            model->hot_values[i] = le_read_byte(s);
    }
}

