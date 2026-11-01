#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "presco.h"

// Expand the actual production PWM sequence into pin levels. In Nordic's
// edge-aligned mode COUNTERTOP is the period, bit 15 selects falling-edge
// polarity, and a compare above TOP holds the initial level for that period.
static size_t expand(const nrf_pwm_sequence_t *seq, uint8_t *levels, size_t capacity) {
    size_t count = 0;
    assert(seq->length % 4 == 0);
    for (size_t i = 0; i < seq->length / 4; i++) {
        const nrf_pwm_values_wave_form_t *v = &seq->values.p_wave_form[i];
        uint16_t compare = v->channel_0 & 0x7FFF;
        bool falling = (v->channel_0 & 0x8000) != 0;
        assert(v->counter_top >= 3);
        assert(v->channel_1 == 0 && v->channel_2 == 0);
        for (uint16_t t = 0; t < v->counter_top; t++) {
            assert(count < capacity);
            levels[count++] = falling ? t < compare : t >= compare;
        }
    }
    return count;
}

static void check_code(uint32_t code) {
    uint8_t data[4] = {code >> 24, code >> 16, code >> 8, code};
    uint32_t blocks[6] = {0, 0, 0, 0, 0, 0xDEADBEEF};
    assert(presco_t55xx_writer(data, blocks) == 5);
    // RF/32, Manchester, terminator, password, max block 4.
    assert(blocks[0] == 0x00088098);
    assert(blocks[1] == 0x10D00000 && blocks[2] == 0 && blocks[3] == 0);
    assert(blocks[4] == code && blocks[5] == 0xDEADBEEF);

    nrf_pwm_sequence_t *seq = presco.modulator(NULL, data);
    assert(seq != NULL && seq->length == 131 * 4);
    assert(seq->repeats == 0 && seq->end_delay == 0);
    uint8_t levels[132 * 32];
    assert(expand(seq, levels, sizeof(levels)) == sizeof(levels));
    // Independently decode Manchester half-bit levels, then compare the
    // decoded data against literal expected frame words (not another encoder).
    uint32_t decoded[4] = {0};
    for (size_t bit = 0; bit < 128; bit++) {
        bool first = levels[bit * 32];
        for (size_t tick = 0; tick < 32; tick++) {
            assert(levels[bit * 32 + tick] == (tick < 16 ? first : !first));
        }
        decoded[bit / 32] = (decoded[bit / 32] << 1) | first;
    }
    assert(decoded[0] == 0x10D00000 && decoded[1] == 0 && decoded[2] == 0);
    assert(decoded[3] == code);
    // Proxmark stAskSimBit: H16 L16 H48 L16 H32. Check every tick.
    for (size_t tick = 0; tick < 128; tick++) {
        bool high = tick < 16 || (tick >= 32 && tick < 80) || tick >= 96;
        assert(levels[4096 + tick] == high);
    }
    // Last terminator level is high; next frame starts low. This verifies
    // the intended transition across the loop boundary for any code value.
    assert(levels[sizeof(levels) - 1] == 1 && levels[0] == 0);
}

int main(void) {
    assert(presco.tag_type == TAG_TYPE_PRESCO && presco.data_size == 4);
    void *codec = presco.alloc();
    assert(codec != NULL);
    uint8_t zeros[4] = {0};
    assert(memcmp(presco.get_data(codec), zeros, 4) == 0);
    presco.decoder.start(codec, 0);
    assert(!presco.decoder.feed(codec, 32));
    presco.free(codec);
    check_code(0);
    check_code(0xFFFFFFFF);
    check_code(0x039CA0CF); // printed 018363467
    check_code(0x12345678); // nonzero unknown byte must survive
    check_code(0xAAAAAAAA);
    check_code(0x55555555);
    for (unsigned bit = 0; bit < 32; bit++) {
        check_code(UINT32_C(1) << bit);
        check_code(~(UINT32_C(1) << bit));
    }
    puts("Presco frame, T5577 configuration, PWM and loop-boundary tests passed");
    return 0;
}
