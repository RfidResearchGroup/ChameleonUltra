#ifndef CCID_TRANSPORT_TEST_APP_USBD_H
#define CCID_TRANSPORT_TEST_APP_USBD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* These stubs model the SDK's asynchronous transfer contract, not its DMA. */
typedef uint32_t ret_code_t;
typedef uint8_t nrf_drv_usbd_ep_t;
typedef uint8_t nrf_drv_usbd_ep_status_t;

#define NRF_SUCCESS 0U
#define NRF_ERROR_BUSY 17U
#define NRF_ERROR_INVALID_STATE 8U
#define NRF_ERROR_INVALID_LENGTH 9U
#define NRF_ERROR_NOT_SUPPORTED 10U
#define NRF_USBD_EP_OK 0U
#define NRF_USBD_EP_WAITING 1U
#define NRF_USBD_EP_ABORTED 3U
#define NRF_DRV_USBD_EPOUT1 0x01U
#define NRF_DRV_USBD_EPIN1 0x81U
#define NRF_DRV_USBD_EPIN2 0x82U
#define NRF_DRV_USBD_EPOUT3 0x03U
#define NRF_DRV_USBD_EPIN3 0x83U
#define NRF_DRV_USBD_EPIN4 0x84U
#define NRF_DRV_USBD_EPSIZE 64U
#define STATIC_ASSERT(expression) _Static_assert(expression, #expression)
#define UNUSED_PARAMETER(value) ((void)(value))
#define LSB_16(value) ((uint8_t)(value))
#define MSB_16(value) ((uint8_t)((value) >> 8U))

enum {
    APP_USBD_EVT_INST_APPEND,
    APP_USBD_EVT_INST_REMOVE,
    APP_USBD_EVT_STARTED,
    APP_USBD_EVT_DRV_RESET,
    APP_USBD_EVT_STOPPED,
    APP_USBD_EVT_POWER_REMOVED,
    APP_USBD_EVT_DRV_SUSPEND,
    APP_USBD_EVT_DRV_RESUME,
    APP_USBD_EVT_STATE_CHANGED,
    APP_USBD_EVT_DRV_SETUP,
    APP_USBD_EVT_DRV_EPTRANSFER,
};

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    struct { uint16_t w; } wValue, wIndex, wLength;
} app_usbd_setup_t;

typedef struct {
    uint32_t type;
    app_usbd_setup_t setup;
} app_usbd_setup_evt_t;

typedef union {
    struct { uint32_t type; } app_evt;
    struct {
        uint32_t type;
        struct {
            struct {
                nrf_drv_usbd_ep_t ep;
                nrf_drv_usbd_ep_status_t status;
            } eptransfer;
        } data;
    } drv_evt;
    app_usbd_setup_evt_t setup_evt;
} app_usbd_complex_evt_t;

typedef struct {
    size_t offset;
} app_usbd_class_descriptor_ctx_t;

struct app_usbd_class_methods_s;
typedef struct {
    void *p_data;
    const struct app_usbd_class_methods_s *p_class_methods;
} app_usbd_class_inst_t;

typedef struct app_usbd_class_methods_s {
    ret_code_t (*event_handler)(const app_usbd_class_inst_t *, const app_usbd_complex_evt_t *);
    bool (*feed_descriptors)(
        app_usbd_class_descriptor_ctx_t *, const app_usbd_class_inst_t *, uint8_t *, size_t
    );
} app_usbd_class_methods_t;

#define APP_USBD_CLASS_INSTANCE_SPECIFIC_DEC_NONE
#define APP_USBD_CLASS_TYPEDEF(name, config, instance_fields, data_fields) \
    typedef struct { data_fields } name##_data_t; \
    typedef union { \
        app_usbd_class_inst_t base; \
        struct { \
            name##_data_t *p_data; \
            const app_usbd_class_methods_t *p_class_methods; \
        } specific; \
    } name##_t

#define APP_USBD_CLASS_INST_GLOBAL_DEF(instance, name, methods, config, extra) \
    static name##_data_t instance##_data; \
    static const name##_t instance = { \
        .specific = {.p_data = &instance##_data, .p_class_methods = methods} \
    }

typedef struct { nrf_drv_usbd_ep_t address; } app_usbd_class_ep_conf_t;
typedef struct { app_usbd_class_ep_conf_t endpoints[3]; } app_usbd_class_iface_conf_t;

const app_usbd_class_iface_conf_t *app_usbd_class_iface_get(
    const app_usbd_class_inst_t *instance, uint8_t index
);
const app_usbd_class_ep_conf_t *app_usbd_class_iface_ep_get(
    const app_usbd_class_iface_conf_t *interface, uint8_t index
);
nrf_drv_usbd_ep_t app_usbd_class_ep_address_get(const app_usbd_class_ep_conf_t *endpoint);

typedef struct {
    union { void *rx; const void *tx; } p_data;
    size_t size;
} nrf_drv_usbd_transfer_t;

#define NRF_DRV_USBD_TRANSFER_OUT(name, pointer, length) \
    nrf_drv_usbd_transfer_t name = {.p_data = {.rx = pointer}, .size = length}
#define NRF_DRV_USBD_TRANSFER_IN(name, pointer, length) \
    nrf_drv_usbd_transfer_t name = {.p_data = {.tx = pointer}, .size = length}

ret_code_t app_usbd_ep_transfer(nrf_drv_usbd_ep_t endpoint, const nrf_drv_usbd_transfer_t *transfer);
nrf_drv_usbd_ep_status_t nrf_drv_usbd_ep_status_get(nrf_drv_usbd_ep_t endpoint, size_t *size);
size_t nrf_drv_usbd_epout_size_get(nrf_drv_usbd_ep_t endpoint);

#endif
