#include "usb_ccid.h"

#if defined(PROJECT_CHAMELEON_ULTRA)

#include <string.h>

#include "app_timer.h"
#include "app_usbd_class_base.h"
#include "app_usbd_core.h"
#include "app_usbd_descriptor.h"
#include "usb_ccid_protocol.h"

/* CDC uses interfaces 0/1, bulk EP1 and interrupt EP2. Keep its layout intact. */
#define CCID_INTERFACE          2U
#define CCID_BULK_OUT           NRF_DRV_USBD_EPOUT3
#define CCID_BULK_IN            NRF_DRV_USBD_EPIN3
#define CCID_INTERRUPT_IN       NRF_DRV_USBD_EPIN4
#define CCID_POLL_INTERVAL_MS   250U

#define CCID_CONTROL_ABORT      0x01U
#define CCID_CONTROL_CLOCKS     0x02U
#define CCID_CONTROL_DATA_RATES 0x03U

typedef enum {
    CCID_IN_IDLE = 0,
    CCID_IN_DATA,
    CCID_IN_ZLP,
} ccid_in_state_t;

typedef struct {
    bool configured;
    bool suspended;
    bool out_armed;
    bool interrupt_busy;
    bool command_pending;
    bool response_pending;
    ccid_in_state_t in_state;
    uint8_t packet[USB_CCID_PACKET_SIZE];
    uint8_t interrupt_data[2];
    usb_ccid_rx_t rx;
    uint32_t last_poll_tick;
    usb_ccid_protocol_t protocol;
} ccid_ctx_t;

#define APP_USBD_CCID_CONFIG(iface, epout, epin, epint) ((iface, epout, epin, epint))
#define APP_USBD_CCID_DATA_SPECIFIC_DEC ccid_ctx_t ctx;

APP_USBD_CLASS_TYPEDEF(
    app_usbd_ccid,
    APP_USBD_CCID_CONFIG(0, NRF_DRV_USBD_EPOUT1, NRF_DRV_USBD_EPIN1, NRF_DRV_USBD_EPIN2),
    APP_USBD_CLASS_INSTANCE_SPECIFIC_DEC_NONE,
    APP_USBD_CCID_DATA_SPECIFIC_DEC
);

static ret_code_t ccid_event(
    app_usbd_class_inst_t const *inst,
    app_usbd_complex_evt_t const *event
);
static bool ccid_descriptors(
    app_usbd_class_descriptor_ctx_t *ctx,
    app_usbd_class_inst_t const *inst,
    uint8_t *buffer,
    size_t capacity
);

static const app_usbd_class_methods_t m_ccid_methods = {
    .event_handler = ccid_event,
    .feed_descriptors = ccid_descriptors,
};

APP_USBD_CLASS_INST_GLOBAL_DEF(
    m_ccid, app_usbd_ccid, &m_ccid_methods,
    APP_USBD_CCID_CONFIG(CCID_INTERFACE, CCID_BULK_OUT, CCID_BULK_IN, CCID_INTERRUPT_IN),
    ()
);

/*
 * CCID 1.10: one slot, fixed T=1 parameters, short APDU exchange (0x00020000).
 * This presents the PC/SC storage-card UID command; it does not expose ISO-DEP
 * APDU pass-through. dwMaxCCIDMessageLength includes the ten-byte header.
 */
static const uint8_t m_descriptor[] = {
    0x09, APP_USBD_DESCRIPTOR_INTERFACE,
    CCID_INTERFACE, 0x00, 0x03, 0x0B, 0x00, 0x00, 0x00,

    0x36, 0x21, 0x10, 0x01, 0x00, 0x07,
    0x02, 0x00, 0x00, 0x00,
    0xA0, 0x0F, 0x00, 0x00,
    0xA0, 0x0F, 0x00, 0x00, 0x00,
    0x80, 0x25, 0x00, 0x00,
    0x80, 0x25, 0x00, 0x00, 0x00,
    0xFE, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x42, 0x00, 0x02, 0x00,
    LSB_16(USB_CCID_MAX_MESSAGE), MSB_16(USB_CCID_MAX_MESSAGE), 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01,

    0x07, APP_USBD_DESCRIPTOR_ENDPOINT,
    CCID_BULK_OUT, APP_USBD_DESCRIPTOR_EP_ATTR_TYPE_BULK,
    LSB_16(NRF_DRV_USBD_EPSIZE), MSB_16(NRF_DRV_USBD_EPSIZE), 0x00,

    0x07, APP_USBD_DESCRIPTOR_ENDPOINT,
    CCID_BULK_IN, APP_USBD_DESCRIPTOR_EP_ATTR_TYPE_BULK,
    LSB_16(NRF_DRV_USBD_EPSIZE), MSB_16(NRF_DRV_USBD_EPSIZE), 0x00,

    0x07, APP_USBD_DESCRIPTOR_ENDPOINT,
    CCID_INTERRUPT_IN, APP_USBD_DESCRIPTOR_EP_ATTR_TYPE_INTERRUPT,
    0x02, 0x00, 0x10,
};

STATIC_ASSERT(sizeof(m_descriptor) == 84U);
STATIC_ASSERT(USB_CCID_PACKET_SIZE == NRF_DRV_USBD_EPSIZE);

static ccid_ctx_t *context(app_usbd_class_inst_t const *inst) {
    return &((app_usbd_ccid_t const *)inst)->specific.p_data->ctx;
}

static nrf_drv_usbd_ep_t endpoint(app_usbd_class_inst_t const *inst, uint8_t index) {
    app_usbd_class_iface_conf_t const *iface = app_usbd_class_iface_get(inst, 0);
    return app_usbd_class_ep_address_get(app_usbd_class_iface_ep_get(iface, index));
}

static ret_code_t arm_out(app_usbd_class_inst_t const *inst) {
    ccid_ctx_t *ctx = context(inst);
    if (!ctx->configured || ctx->suspended || ctx->out_armed ||
            ctx->command_pending || ctx->response_pending ||
            ctx->protocol.waiting_abort || ctx->in_state != CCID_IN_IDLE) {
        return NRF_SUCCESS;
    }
    NRF_DRV_USBD_TRANSFER_OUT(transfer, ctx->packet, sizeof(ctx->packet));
    ret_code_t result = app_usbd_ep_transfer(endpoint(inst, 0U), &transfer);
    if (result == NRF_SUCCESS) {
        ctx->out_armed = true;
    }
    return result;
}

static ret_code_t send_response(app_usbd_class_inst_t const *inst) {
    ccid_ctx_t *ctx = context(inst);
    NRF_DRV_USBD_TRANSFER_IN(transfer, ctx->protocol.tx, ctx->protocol.tx_length);
    ret_code_t result = app_usbd_ep_transfer(endpoint(inst, 1U), &transfer);
    if (result == NRF_SUCCESS) {
        ctx->in_state = CCID_IN_DATA;
    }
    return result;
}

static void notify_slot(app_usbd_class_inst_t const *inst) {
    ccid_ctx_t *ctx = context(inst);
    if (!ctx->configured || ctx->suspended || ctx->interrupt_busy ||
            !ctx->protocol.slot_changed) {
        return;
    }
    ctx->interrupt_data[0] = 0x50U; /* RDR_to_PC_NotifySlotChange */
    ctx->interrupt_data[1] = ctx->protocol.present ? 0x03U : 0x02U;
    NRF_DRV_USBD_TRANSFER_IN(transfer, ctx->interrupt_data, sizeof(ctx->interrupt_data));
    if (app_usbd_ep_transfer(endpoint(inst, 2U), &transfer) == NRF_SUCCESS) {
        ctx->interrupt_busy = true;
        ctx->protocol.slot_changed = false;
    }
}

static ret_code_t setup_request(
    app_usbd_class_inst_t const *inst,
    app_usbd_setup_evt_t const *event
) {
    app_usbd_setup_t const *setup = &event->setup;
    if (app_usbd_setup_req_rec(setup->bmRequestType) != APP_USBD_SETUP_REQREC_INTERFACE ||
            app_usbd_setup_req_typ(setup->bmRequestType) != APP_USBD_SETUP_REQTYPE_CLASS ||
            setup->wIndex.w != CCID_INTERFACE) {
        return NRF_ERROR_NOT_SUPPORTED;
    }
    if (app_usbd_setup_req_dir(setup->bmRequestType) == APP_USBD_SETUP_REQDIR_IN &&
            setup->wValue.w == 0U) {
        /* nRF52840 is little endian, as required by the CCID control reply. */
        static const uint32_t clock = USB_CCID_CLOCK_KHZ;
        static const uint32_t rate = USB_CCID_DATA_RATE_BPS;
        if (setup->bRequest == CCID_CONTROL_CLOCKS) {
            return app_usbd_core_setup_rsp(setup, &clock, sizeof(clock));
        }
        if (setup->bRequest == CCID_CONTROL_DATA_RATES) {
            return app_usbd_core_setup_rsp(setup, &rate, sizeof(rate));
        }
    }
    if (app_usbd_setup_req_dir(setup->bmRequestType) == APP_USBD_SETUP_REQDIR_OUT &&
            setup->bRequest == CCID_CONTROL_ABORT && setup->wLength.w == 0U &&
            (uint8_t)setup->wValue.w == 0U) {
        ccid_ctx_t *ctx = context(inst);
        bool was_waiting = ctx->protocol.waiting_abort;
        usb_ccid_protocol_abort(&ctx->protocol, (uint8_t)(setup->wValue.w >> 8U));
        if (was_waiting && !ctx->protocol.waiting_abort &&
                !ctx->command_pending && ctx->in_state == CCID_IN_IDLE) {
            ctx->response_pending = true;
        }
        return NRF_SUCCESS;
    }
    return NRF_ERROR_NOT_SUPPORTED;
}

static ret_code_t endpoint_event(
    app_usbd_class_inst_t const *inst,
    app_usbd_complex_evt_t const *event
) {
    ccid_ctx_t *ctx = context(inst);
    nrf_drv_usbd_ep_t ep = event->drv_evt.data.eptransfer.ep;
    nrf_drv_usbd_ep_status_t status = event->drv_evt.data.eptransfer.status;
    if (ep == endpoint(inst, 0U)) {
        if (status == NRF_USBD_EP_WAITING) {
            return arm_out(inst);
        }
        ctx->out_armed = false;
        if (status == NRF_USBD_EP_OK) {
            size_t received = 0U;
            if (nrf_drv_usbd_ep_status_get(ep, &received) != NRF_SUCCESS) {
                received = nrf_drv_usbd_epout_size_get(ep);
            }
            if (received == 0U && ctx->rx.length == 0U) {
                /* Some hosts append a ZLP after a complete full-packet message. */
                return arm_out(inst);
            }
            if (received > sizeof(ctx->packet)) {
                return NRF_ERROR_INVALID_LENGTH;
            }
            ctx->command_pending = usb_ccid_rx_feed(&ctx->rx, ctx->packet, received);
            /*
             * RF work runs from the main loop, after USB events are drained.
             * Do not rearm OUT until IN completes: otherwise a second command
             * could overwrite the message or response that the driver owns.
             */
            return ctx->command_pending ? NRF_SUCCESS : arm_out(inst);
        }
        ctx->rx.length = 0U;
        return arm_out(inst);
    }
    if (ep == endpoint(inst, 1U)) {
        if (status == NRF_USBD_EP_OK && ctx->in_state == CCID_IN_DATA &&
                ctx->protocol.tx_length % NRF_DRV_USBD_EPSIZE == 0U) {
            /* A full-size last packet needs a ZLP to terminate the message. */
            NRF_DRV_USBD_TRANSFER_IN(transfer, NULL, 0U);
            ret_code_t result = app_usbd_ep_transfer(ep, &transfer);
            if (result == NRF_SUCCESS) {
                ctx->in_state = CCID_IN_ZLP;
                return NRF_SUCCESS;
            }
        }
        ctx->in_state = CCID_IN_IDLE;
        return arm_out(inst);
    }
    if (ep == endpoint(inst, 2U)) {
        ctx->interrupt_busy = false;
        if (status != NRF_USBD_EP_OK) {
            ctx->protocol.slot_changed = true;
        }
        notify_slot(inst);
        return NRF_SUCCESS;
    }
    return NRF_ERROR_NOT_SUPPORTED;
}

static void reset(ccid_ctx_t *ctx) {
    /* Only release CCID's RF ownership; CDC keep-field sessions survive. */
    usb_ccid_reader_ccid_reset();
    memset(ctx, 0, sizeof(*ctx));
}

static ret_code_t ccid_event(
    app_usbd_class_inst_t const *inst,
    app_usbd_complex_evt_t const *event
) {
    ccid_ctx_t *ctx = context(inst);
    switch (event->app_evt.type) {
        case APP_USBD_EVT_INST_APPEND:
        case APP_USBD_EVT_INST_REMOVE:
        case APP_USBD_EVT_STARTED:
            return NRF_SUCCESS;
        case APP_USBD_EVT_DRV_RESET:
        case APP_USBD_EVT_STOPPED:
        case APP_USBD_EVT_POWER_REMOVED:
            reset(ctx);
            return NRF_SUCCESS;
        case APP_USBD_EVT_DRV_SUSPEND:
            ctx->suspended = true;
            /*
             * The SDK aborts endpoint DMA on suspend. Discard queued replies
             * rather than later delivering a pre-suspend active-card status.
             */
            ctx->command_pending = false;
            ctx->response_pending = false;
            ctx->out_armed = false;
            ctx->in_state = CCID_IN_IDLE;
            ctx->interrupt_busy = false;
            ctx->rx.length = 0U;
            memset(&ctx->protocol, 0, sizeof(ctx->protocol));
            ctx->protocol.slot_changed = true;
            usb_ccid_reader_ccid_reset();
            usb_ccid_protocol_sync(&ctx->protocol);
            return NRF_SUCCESS;
        case APP_USBD_EVT_DRV_RESUME:
            ctx->suspended = false;
            ctx->last_poll_tick = app_timer_cnt_get() - APP_TIMER_TICKS(CCID_POLL_INTERVAL_MS);
            (void)arm_out(inst);
            return NRF_SUCCESS;
        case APP_USBD_EVT_STATE_CHANGED:
            if (app_usbd_core_state_get() == APP_USBD_STATE_Configured) {
                ctx->configured = true;
                ctx->protocol.slot_changed = true;
                ctx->last_poll_tick = app_timer_cnt_get() - APP_TIMER_TICKS(CCID_POLL_INTERVAL_MS);
                (void)arm_out(inst);
            } else {
                reset(ctx);
            }
            return NRF_SUCCESS;
        case APP_USBD_EVT_DRV_SETUP:
            return setup_request(inst, &event->setup_evt);
        case APP_USBD_EVT_DRV_EPTRANSFER:
            return endpoint_event(inst, event);
        default:
            return NRF_ERROR_NOT_SUPPORTED;
    }
}

static bool ccid_descriptors(
    app_usbd_class_descriptor_ctx_t *ctx,
    app_usbd_class_inst_t const *inst,
    uint8_t *buffer,
    size_t capacity
) {
    UNUSED_PARAMETER(inst);
    static uint8_t index;
    APP_USBD_CLASS_DESCRIPTOR_BEGIN(ctx, buffer, capacity)
    for (index = 0U; index < sizeof(m_descriptor); index++) {
        APP_USBD_CLASS_DESCRIPTOR_WRITE(m_descriptor[index]);
    }
    APP_USBD_CLASS_DESCRIPTOR_END();
}

app_usbd_class_inst_t const *usb_ccid_class_inst_get(void) {
    return &m_ccid.base;
}

void usb_ccid_process(void) {
    app_usbd_class_inst_t const *inst = &m_ccid.base;
    ccid_ctx_t *ctx = context(inst);
    if (!ctx->configured || ctx->suspended) {
        return;
    }
    if (ctx->command_pending && ctx->in_state == CCID_IN_IDLE) {
        usb_ccid_protocol_process(&ctx->protocol, ctx->rx.bytes, ctx->rx.length);
        ctx->rx.length = 0U;
        ctx->command_pending = false;
        ctx->response_pending = ctx->protocol.tx_length != 0U;
    }
    if (ctx->response_pending) {
        /* Retry the USB transfer, never execute an RF command twice. */
        if (send_response(inst) == NRF_SUCCESS) {
            ctx->response_pending = false;
        }
        notify_slot(inst);
        return;
    }
    usb_ccid_protocol_sync(&ctx->protocol);
    if (ctx->protocol.waiting_abort) {
        return;
    }
    if (ctx->in_state == CCID_IN_IDLE) {
        uint32_t now = app_timer_cnt_get();
        if (app_timer_cnt_diff_compute(now, ctx->last_poll_tick) >= APP_TIMER_TICKS(CCID_POLL_INTERVAL_MS)) {
            ctx->last_poll_tick = now;
            bool present;
            (void)usb_ccid_reader_poll(&present);
            usb_ccid_protocol_sync(&ctx->protocol);
        }
    }
    notify_slot(inst);
    (void)arm_out(inst);
}

#endif
