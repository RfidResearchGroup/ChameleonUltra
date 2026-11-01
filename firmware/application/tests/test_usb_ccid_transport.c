#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_usbd.h"
#include "usb_ccid_protocol.h"

/* Include the real transport to exercise its static event handlers unchanged. */
#include "../src/usb_ccid.c"

typedef struct {
    bool busy;
    void *pointer;
    size_t length;
    size_t received;
    unsigned attempts;
    unsigned successes;
    unsigned fail_next;
    ret_code_t failure;
    uint8_t snapshot[USB_CCID_MAX_MESSAGE];
} mock_endpoint_t;

static mock_endpoint_t endpoints[16];
static uint8_t usb_state;
static uint32_t clock_tick;
static bool reader_present;
static bool reader_active;
static unsigned reader_power_on_count;
static unsigned reader_poll_count;
static unsigned reader_reset_count;
static size_t mock_exchange_length;
static unsigned reader_exchange_count;

static size_t ep_index(nrf_drv_usbd_ep_t endpoint_address) {
    return (endpoint_address & 0x07U) + ((endpoint_address & 0x80U) != 0U ? 8U : 0U);
}

static mock_endpoint_t *ep_state(nrf_drv_usbd_ep_t endpoint_address) {
    return &endpoints[ep_index(endpoint_address)];
}

const app_usbd_class_iface_conf_t *app_usbd_class_iface_get(
    const app_usbd_class_inst_t *instance,
    uint8_t index
) {
    static const app_usbd_class_iface_conf_t interface = {
        .endpoints = {{0x03U}, {0x83U}, {0x84U}},
    };
    (void)instance;
    assert(index == 0U);
    return &interface;
}

const app_usbd_class_ep_conf_t *app_usbd_class_iface_ep_get(
    const app_usbd_class_iface_conf_t *interface,
    uint8_t index
) {
    assert(index < 3U);
    return &interface->endpoints[index];
}

nrf_drv_usbd_ep_t app_usbd_class_ep_address_get(const app_usbd_class_ep_conf_t *ep) {
    return ep->address;
}

ret_code_t app_usbd_ep_transfer(nrf_drv_usbd_ep_t endpoint_address, const nrf_drv_usbd_transfer_t *transfer) {
    mock_endpoint_t *ep = ep_state(endpoint_address);
    ep->attempts++;
    if (ep->fail_next != 0U) {
        ep->fail_next--;
        return ep->failure != NRF_SUCCESS ? ep->failure : NRF_ERROR_BUSY;
    }
    if (ep->busy) {
        return NRF_ERROR_BUSY;
    }
    assert(transfer->size <= sizeof(ep->snapshot));
    ep->pointer = transfer->p_data.rx;
    ep->length = transfer->size;
    ep->busy = true;
    ep->successes++;
    if ((endpoint_address & 0x80U) != 0U && ep->length != 0U) {
        memcpy(ep->snapshot, transfer->p_data.tx, ep->length);
    }
    return NRF_SUCCESS;
}

nrf_drv_usbd_ep_status_t nrf_drv_usbd_ep_status_get(nrf_drv_usbd_ep_t endpoint_address, size_t *size) {
    *size = ep_state(endpoint_address)->received;
    return NRF_USBD_EP_OK;
}

size_t nrf_drv_usbd_epout_size_get(nrf_drv_usbd_ep_t endpoint_address) {
    return ep_state(endpoint_address)->received;
}

uint8_t app_usbd_core_state_get(void) {
    return usb_state;
}

ret_code_t app_usbd_core_setup_rsp(const app_usbd_setup_t *setup, const void *data, size_t size) {
    (void)setup;
    (void)data;
    (void)size;
    return NRF_SUCCESS;
}

uint32_t app_timer_cnt_get(void) {
    return clock_tick;
}

uint32_t app_timer_cnt_diff_compute(uint32_t now, uint32_t previous) {
    return now - previous;
}

void usb_ccid_reader_get_state(bool *present, bool *active) {
    *present = reader_present;
    *active = reader_active;
}

void usb_ccid_reader_ccid_reset(void) {
    reader_reset_count++;
    reader_present = false;
    reader_active = false;
}

usb_ccid_reader_result_t usb_ccid_reader_poll(bool *present) {
    reader_poll_count++;
    *present = reader_present;
    return reader_present ? USB_CCID_READER_OK : USB_CCID_READER_NO_CARD;
}

usb_ccid_reader_result_t usb_ccid_reader_power_on(uint8_t *atr, size_t capacity, size_t *length) {
    reader_power_on_count++;
    assert(capacity >= 20U);
    memset(atr, 0, 20U);
    atr[0] = 0x3BU;
    *length = 20U;
    reader_present = true;
    reader_active = true;
    return USB_CCID_READER_OK;
}

void usb_ccid_reader_power_off(void) {
    reader_active = false;
}

usb_ccid_reader_result_t usb_ccid_reader_transceive(
    const uint8_t *apdu, size_t length, uint8_t *response,
    size_t capacity, size_t *response_length
) {
    (void)apdu;
    (void)length;
    reader_exchange_count++;
    assert(capacity >= mock_exchange_length);
    memset(response, 0x5A, mock_exchange_length);
    *response_length = mock_exchange_length;
    return USB_CCID_READER_OK;
}

static void event(uint32_t type) {
    app_usbd_complex_evt_t ev = {.app_evt = {.type = type}};
    assert(m_ccid_methods.event_handler(&m_ccid.base, &ev) == NRF_SUCCESS);
}

static void endpoint_event_inject(nrf_drv_usbd_ep_t address, nrf_drv_usbd_ep_status_t status) {
    app_usbd_complex_evt_t ev = {
        .drv_evt = {
            .type = APP_USBD_EVT_DRV_EPTRANSFER,
            .data = {.eptransfer = {.ep = address, .status = status}},
        },
    };
    assert(m_ccid_methods.event_handler(&m_ccid.base, &ev) == NRF_SUCCESS);
}

static void complete_in(nrf_drv_usbd_ep_t address) {
    mock_endpoint_t *ep = ep_state(address);
    assert(ep->busy);
    /* Submitted RAM remains owned by the driver until completion. */
    if (ep->length != 0U) {
        assert(memcmp(ep->pointer, ep->snapshot, ep->length) == 0);
    }
    ep->busy = false;
    endpoint_event_inject(address, NRF_USBD_EP_OK);
}

static void receive_out(const uint8_t *bytes, size_t length) {
    mock_endpoint_t *ep = ep_state(CCID_BULK_OUT);
    assert(ep->busy);
    assert(length <= ep->length);
    memcpy(ep->pointer, bytes, length);
    ep->received = length;
    ep->busy = false;
    endpoint_event_inject(CCID_BULK_OUT, NRF_USBD_EP_OK);
}

static void fixture(void) {
    memset(&m_ccid_data, 0, sizeof(m_ccid_data));
    memset(endpoints, 0, sizeof(endpoints));
    usb_state = APP_USBD_STATE_Configured;
    clock_tick = 1000U;
    reader_present = false;
    reader_active = false;
    reader_power_on_count = 0U;
    reader_poll_count = 0U;
    reader_reset_count = 0U;
    reader_exchange_count = 0U;
    mock_exchange_length = 6U;
    event(APP_USBD_EVT_STATE_CHANGED);
    assert(context(&m_ccid.base)->configured);
    assert(ep_state(CCID_BULK_OUT)->busy);
}

static void test_out_process_in_and_buffer_ownership(void) {
    fixture();
    static const uint8_t power_on[] = {0x62U, 0, 0, 0, 0, 0, 0x31U, 0, 0, 0};
    receive_out(power_on, sizeof(power_on));
    ccid_ctx_t *ctx = context(&m_ccid.base);
    assert(ctx->command_pending && !ctx->out_armed);
    assert(reader_power_on_count == 0U);
    usb_ccid_process();
    assert(reader_power_on_count == 1U);
    assert(ctx->in_state == CCID_IN_DATA);
    assert(ep_state(CCID_BULK_IN)->snapshot[0] == 0x80U);
    assert(ep_state(CCID_BULK_IN)->snapshot[6] == 0x31U);
    assert(ep_state(CCID_BULK_OUT)->busy);
    usb_ccid_process();
    assert(reader_power_on_count == 1U);
    complete_in(CCID_BULK_IN);
    assert(ctx->in_state == CCID_IN_IDLE);
    assert(ep_state(CCID_BULK_OUT)->busy);
}

static void test_slot_notification_is_queued_and_immutable(void) {
    fixture();
    usb_ccid_process();
    mock_endpoint_t *interrupt = ep_state(CCID_INTERRUPT_IN);
    assert(interrupt->busy && interrupt->length == 2U);
    assert(interrupt->snapshot[0] == 0x50U && interrupt->snapshot[1] == 0x02U);
    reader_present = true;
    clock_tick += 250U;
    usb_ccid_process();
    assert(context(&m_ccid.base)->protocol.slot_changed);
    assert(interrupt->snapshot[1] == 0x02U);
    complete_in(CCID_INTERRUPT_IN);
    assert(interrupt->busy && interrupt->snapshot[1] == 0x03U);
    complete_in(CCID_INTERRUPT_IN);
    assert(!interrupt->busy);
}

static void test_suspend_resume_and_reset(void) {
    fixture();
    usb_ccid_process();
    memset(endpoints, 0, sizeof(endpoints)); /* The SDK aborts DMA on suspend. */
    event(APP_USBD_EVT_DRV_SUSPEND);
    ccid_ctx_t *ctx = context(&m_ccid.base);
    assert(ctx->suspended && !ctx->out_armed && !ctx->interrupt_busy);
    assert(reader_reset_count == 1U);
    unsigned before = reader_poll_count;
    usb_ccid_process();
    assert(reader_poll_count == before);
    event(APP_USBD_EVT_DRV_RESUME);
    assert(!ctx->suspended && ep_state(CCID_BULK_OUT)->busy);
    usb_ccid_process();
    assert(reader_poll_count == before + 1U);
    memset(endpoints, 0, sizeof(endpoints));
    event(APP_USBD_EVT_DRV_RESET);
    assert(!ctx->configured && !ctx->out_armed && !ctx->interrupt_busy);
    usb_ccid_process();
    assert(!ep_state(CCID_BULK_OUT)->busy);
}

static void test_response_retry_does_not_repeat_rf_command(void) {
    fixture();
    static const uint8_t power_on[] = {0x62U, 0, 0, 0, 0, 0, 0x32U, 0, 0, 0};
    receive_out(power_on, sizeof(power_on));
    ep_state(CCID_BULK_IN)->fail_next = 2U;
    usb_ccid_process();
    assert(context(&m_ccid.base)->response_pending);
    assert(reader_power_on_count == 1U);
    usb_ccid_process();
    assert(context(&m_ccid.base)->response_pending);
    assert(reader_power_on_count == 1U);
    usb_ccid_process();
    assert(!context(&m_ccid.base)->response_pending);
    assert(reader_power_on_count == 1U);
    assert(ep_state(CCID_BULK_IN)->successes == 1U);
    complete_in(CCID_BULK_IN);
    assert(ep_state(CCID_BULK_OUT)->busy);
}

static void test_full_packet_out_and_in_zlp(void) {
    fixture();
    reader_present = true;
    reader_active = true;
    uint8_t message[128] = {0x6FU, 118U, 0, 0, 0, 0, 0x33U};
    receive_out(message, 64U);
    assert(!context(&m_ccid.base)->command_pending);
    assert(ep_state(CCID_BULK_OUT)->busy);
    receive_out(&message[64], 64U);
    assert(context(&m_ccid.base)->command_pending);
    mock_exchange_length = 54U; /* A 64-byte reply needs an explicit final ZLP. */
    usb_ccid_process();
    assert(reader_exchange_count == 1U);
    assert(ep_state(CCID_BULK_IN)->length == 64U);
    complete_in(CCID_BULK_IN);
    assert(context(&m_ccid.base)->in_state == CCID_IN_ZLP);
    assert(ep_state(CCID_BULK_IN)->length == 0U);
    assert(ep_state(CCID_BULK_OUT)->busy);
    complete_in(CCID_BULK_IN);
    assert(context(&m_ccid.base)->in_state == CCID_IN_IDLE);
    assert(ep_state(CCID_BULK_OUT)->busy);
}

static void test_waiting_recovers_stale_out_armed_flag(void) {
    fixture();
    /* A current WAITING event repairs a shadow flag left set without a buffer. */
    ep_state(CCID_BULK_OUT)->busy = false;
    assert(context(&m_ccid.base)->out_armed);
    endpoint_event_inject(CCID_BULK_OUT, NRF_USBD_EP_WAITING);
    assert(ep_state(CCID_BULK_OUT)->busy);
    assert(context(&m_ccid.base)->out_armed);
}

static void test_delayed_waiting_preserves_already_armed_driver_buffer(void) {
    fixture();
    mock_endpoint_t *out = ep_state(CCID_BULK_OUT);
    void *original_pointer = out->pointer;
    size_t original_length = out->length;
    unsigned original_successes = out->successes;
    unsigned original_attempts = out->attempts;
    /*
     * The SDK queued WAITING before the successful rearm; by dispatch time its
     * buffer is already busy. Returning BUSY here would make app_usbd STALL OUT.
     */
    endpoint_event_inject(CCID_BULK_OUT, NRF_USBD_EP_WAITING);
    assert(context(&m_ccid.base)->out_armed);
    assert(out->busy && out->pointer == original_pointer);
    assert(out->length == original_length);
    assert(out->successes == original_successes);
    assert(out->attempts == original_attempts + 1U);

    static const uint8_t power_on[] = {0x62U, 0, 0, 0, 0, 0, 0x37U, 0, 0, 0};
    receive_out(power_on, sizeof(power_on));
    usb_ccid_process();
    assert(reader_power_on_count == 1U);
    complete_in(CCID_BULK_IN);
    assert(out->busy);
}

static void test_transient_rearm_failure_is_retried_by_main_loop(void) {
    for (unsigned scenario = 0U; scenario < 4U; scenario++) {
        fixture();
        mock_endpoint_t *out = ep_state(CCID_BULK_OUT);
        out->fail_next = 1U;
        out->failure = NRF_ERROR_INVALID_STATE;
        if (scenario == 0U) {
            out->busy = false;
            endpoint_event_inject(CCID_BULK_OUT, NRF_USBD_EP_WAITING);
        } else if (scenario == 1U) {
            uint8_t partial[64] = {0x6FU, 118U, 0, 0, 0, 0, 0x38U};
            receive_out(partial, sizeof(partial));
            assert(context(&m_ccid.base)->rx.length == sizeof(partial));
        } else if (scenario == 2U) {
            uint8_t ignored_zlp = 0U;
            receive_out(&ignored_zlp, 0U);
        } else {
            out->busy = false;
            endpoint_event_inject(CCID_BULK_OUT, NRF_USBD_EP_ABORTED);
        }
        /* Event dispatch succeeds so app_usbd does not stall this endpoint. */
        assert(!out->busy && !context(&m_ccid.base)->out_armed);
        assert(out->fail_next == 0U);
        usb_ccid_process();
        assert(out->busy && context(&m_ccid.base)->out_armed);
        assert(reader_power_on_count == 0U && reader_exchange_count == 0U);
        if (scenario == 1U) {
            assert(context(&m_ccid.base)->rx.length == 64U);
            assert(!context(&m_ccid.base)->command_pending);
        }
    }
}

static void test_full_out_packet_accepts_host_zlp_before_in_done(void) {
    fixture();
    reader_present = true;
    reader_active = true;
    uint8_t message[64] = {0x6FU, 54U, 0, 0, 0, 0, 0x34U};
    receive_out(message, sizeof(message));
    assert(context(&m_ccid.base)->command_pending);
    usb_ccid_process();
    assert(reader_exchange_count == 1U);
    assert(context(&m_ccid.base)->in_state == CCID_IN_DATA);
    assert(ep_state(CCID_BULK_OUT)->busy);

    /*
     * Windows may submit IN only after the ZLP completing its OUT write is
     * ACKed. This packet must be accepted while the previous IN is still live.
     */
    receive_out(message, 0U);
    assert(!context(&m_ccid.base)->command_pending);
    assert(context(&m_ccid.base)->rx.length == 0U);
    assert(ep_state(CCID_BULK_OUT)->busy);
    usb_ccid_process();
    assert(reader_exchange_count == 1U);
    complete_in(CCID_BULK_IN);
    assert(context(&m_ccid.base)->in_state == CCID_IN_IDLE);
    assert(ep_state(CCID_BULK_OUT)->busy);
}

static void test_next_command_waits_for_previous_in_buffer(void) {
    fixture();
    static const uint8_t power_on[] = {0x62U, 0, 0, 0, 0, 0, 0x35U, 0, 0, 0};
    static const uint8_t get_uid_message[] = {
        0x6FU, 5U, 0, 0, 0, 0, 0x36U, 0, 0, 0,
        0xFFU, 0xCAU, 0, 0, 0,
    };
    receive_out(power_on, sizeof(power_on));
    usb_ccid_process();
    assert(reader_power_on_count == 1U);
    assert(ep_state(CCID_BULK_IN)->busy);
    receive_out(get_uid_message, sizeof(get_uid_message));
    ccid_ctx_t *ctx = context(&m_ccid.base);
    assert(ctx->command_pending);
    assert(!ep_state(CCID_BULK_OUT)->busy);
    for (unsigned attempt = 0U; attempt < 3U; attempt++) {
        usb_ccid_process();
        assert(reader_exchange_count == 0U);
        assert(reader_power_on_count == 1U);
        assert(ctx->command_pending);
        assert(ep_state(CCID_BULK_IN)->pointer == ctx->protocol.tx);
        assert(memcmp(ctx->protocol.tx, ep_state(CCID_BULK_IN)->snapshot,
                      ep_state(CCID_BULK_IN)->length) == 0);
    }
    complete_in(CCID_BULK_IN);
    assert(ctx->in_state == CCID_IN_IDLE && ctx->command_pending);
    assert(!ep_state(CCID_BULK_OUT)->busy);
    usb_ccid_process();
    assert(reader_exchange_count == 1U);
    assert(!ctx->command_pending && !ctx->response_pending);
    assert(ep_state(CCID_BULK_IN)->snapshot[6] == 0x36U);
    assert(ep_state(CCID_BULK_OUT)->busy);
    usb_ccid_process();
    assert(reader_exchange_count == 1U);
    complete_in(CCID_BULK_IN);
}

static void test_interrupt_descriptor_terminates_two_byte_notifications(void) {
    uint8_t descriptors[84] = {0};
    app_usbd_class_descriptor_ctx_t descriptor_ctx = {0};
    assert(m_ccid_methods.feed_descriptors(
        &descriptor_ctx, &m_ccid.base, descriptors, sizeof(descriptors)
    ));
    unsigned endpoint_count = 0U;
    bool found_interrupt = false;
    for (size_t offset = 0U; offset < sizeof(descriptors);) {
        size_t length = descriptors[offset];
        assert(length >= 2U && offset + length <= sizeof(descriptors));
        if (descriptors[offset + 1U] == APP_USBD_DESCRIPTOR_ENDPOINT) {
            endpoint_count++;
            if (descriptors[offset + 2U] == CCID_INTERRUPT_IN) {
                found_interrupt = true;
                size_t maximum_packet = descriptors[offset + 4U] |
                                        ((size_t)descriptors[offset + 5U] << 8U);
                assert(maximum_packet == 8U);
                assert(descriptors[offset + 3U] == APP_USBD_DESCRIPTOR_EP_ATTR_TYPE_INTERRUPT);
                /* NotifySlotChange must end as a short packet, not a full one. */
                assert(sizeof(context(&m_ccid.base)->interrupt_data) < maximum_packet);
            }
        }
        offset += length;
    }
    assert(endpoint_count == 3U && found_interrupt);
}

int main(void) {
    test_out_process_in_and_buffer_ownership();
    test_slot_notification_is_queued_and_immutable();
    test_suspend_resume_and_reset();
    test_response_retry_does_not_repeat_rf_command();
    test_full_packet_out_and_in_zlp();
    test_waiting_recovers_stale_out_armed_flag();
    test_delayed_waiting_preserves_already_armed_driver_buffer();
    test_transient_rearm_failure_is_retried_by_main_loop();
    test_full_out_packet_accepts_host_zlp_before_in_done();
    test_next_command_waits_for_previous_in_buffer();
    test_interrupt_descriptor_terminates_two_byte_notifications();
    puts("usb-ccid Nordic transport mock: 11 test groups passed");
    return 0;
}
