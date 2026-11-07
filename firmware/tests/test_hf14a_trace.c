// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint32_t nfc_tag_14a_get_tx_frame_config(void) { return 0x17; }
static uint32_t nfc_tag_14a_get_rx_frame_config(void) { return 0x04; }
#include "trace_subject.c"

int main(void) {
    const uint8_t request = 0x26;
    const uint8_t sak = 0x04;
    m_sniff_active = true;
    hf14a_sniff_frame_cb(&request, 7);
    hf14a_sniff_tx_frame_cb(&sak, 8);
    const uint8_t legacy[] = {0x00, 0x07, 0x26, 0x80, 0x08, 0x04};
    assert(m_sniff_buf_len == sizeof(legacy));
    assert(memcmp(m_sniff_buf, legacy, sizeof(legacy)) == 0);

    m_sniff_buf_len = sizeof(m_sniff_v1_header);
    memcpy(m_sniff_buf, m_sniff_v1_header, sizeof(m_sniff_v1_header));
    m_sniff_metadata = true;
    hf14a_sniff_frame_cb(&request, 7);
    hf14a_sniff_tx_frame_cb(&sak, 8);
    const uint8_t metadata[] = {'C', 'U', '1', '4', 1,
                               0x00, 0x07, 0x04, 0x26,
                               0x80, 0x08, 0x17, 0x04};
    assert(m_sniff_buf_len == sizeof(metadata));
    assert(memcmp(m_sniff_buf, metadata, sizeof(metadata)) == 0);

    m_sniff_buf_len = HF_SNIFF_BUF_SIZE - 1;
    hf14a_sniff_frame_cb(&request, 7);
    assert(m_sniff_buf_len == HF_SNIFF_BUF_SIZE - 1);
    hf14a_sniff_store(NULL, 0, false);
    hf14a_sniff_store(NULL, 0x8000, true);
    assert(m_sniff_buf_len == HF_SNIFF_BUF_SIZE - 1);
    m_sniff_buf_len = 0;
    m_sniff_active = false;
    hf14a_sniff_frame_cb(&request, 7);
    hf14a_sniff_tx_frame_cb(&sak, 8);
    assert(m_sniff_buf_len == 0);
    return 0;
}
