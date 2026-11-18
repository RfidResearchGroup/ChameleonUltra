"""Compile and exercise the production C parser, without Nordic SDK/hardware."""
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class DataframeOriginTest(unittest.TestCase):
    def test_production_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            (work / "utils.h").write_text("""
#include <stddef.h>
#include <string.h>
#define PACKED __attribute__((packed))
#define U16HTONS(x) __builtin_bswap16(x)
#define U16NTOHS(x) __builtin_bswap16(x)
""")
            (work / "nrf_log.h").write_text("""
#define NRF_LOG_MODULE_REGISTER()
#define NRF_LOG_ERROR(...) ((void)0)
#define NRF_LOG_INFO(...) ((void)0)
""")
            for name in ("nrf_log_ctrl.h", "nrf_log_default_backends.h"):
                (work / name).write_text("")
            (work / "app_util_platform.h").write_text("""
void critical_enter(void);
void critical_exit(void);
#define CRITICAL_REGION_ENTER() critical_enter()
#define CRITICAL_REGION_EXIT() critical_exit()
""")
            harness = work / "test.c"
            harness.write_text(r'''
#include <assert.h>
#include <string.h>
#include "dataframe.h"
static unsigned calls;
static data_frame_origin_t expected;
static uint8_t frame[32];
static uint16_t size;
static unsigned locked;
static void (*on_enter)(void), (*on_exit)(void);
void critical_enter(void) {
    if (!locked && on_enter) {
        void (*inject)(void) = on_enter;
        on_enter = NULL;
        inject();
    }
    locked++;
}
void critical_exit(void) {
    assert(locked);
    if (--locked == 0 && on_exit) {
        void (*inject)(void) = on_exit;
        on_exit = NULL;
        inject();
    }
}
static void replace_usb(void) {
    data_frame_disconnect(DATA_FRAME_ORIGIN_USB);
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_BLE);
}
static void receive_ble(void) {
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_BLE);
}
static void disconnect_usb(void) {
    data_frame_disconnect(DATA_FRAME_ORIGIN_USB);
}
static void callback(uint16_t cmd, uint16_t status, uint16_t len, uint8_t *data) {
    assert(!locked); /* callbacks must never run with IRQs masked */
    assert(cmd == 0x1234 && status == 0 && len == 3);
    assert(memcmp(data, "abc", 3) == 0);
    assert(data_frame_get_origin() == expected);
    data_frame_disconnect(expected);
    data_frame_reset();
    data_frame_process(); /* no recursive dispatch */
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_BLE);
    assert(data_frame_get_origin() == expected);
    assert(memcmp(data, "abc", 3) == 0);
    calls++;
}
static void process(data_frame_origin_t origin, unsigned count) {
    expected = origin;
    assert(data_frame_get_origin() == DATA_FRAME_ORIGIN_UNKNOWN);
    data_frame_process();
    assert(calls == count);
    assert(data_frame_get_origin() == DATA_FRAME_ORIGIN_UNKNOWN);
    data_frame_process();
    assert(calls == count);
}
int main(void) {
    uint8_t payload[] = "abc";
    data_frame_tx_t *tx = data_frame_make(0x1234, 0, 3, payload);
    size = tx->length;
    memcpy(frame, tx->buffer, size);
    on_data_frame_complete(callback);
    data_frame_receive_from(frame, 5, DATA_FRAME_ORIGIN_USB);
    data_frame_receive_from(frame + 5, size - 5, DATA_FRAME_ORIGIN_USB);
    process(DATA_FRAME_ORIGIN_USB, 1);
    /* Cross-transport fragments must never complete a command. */
    data_frame_receive_from(frame, 5, DATA_FRAME_ORIGIN_USB);
    data_frame_receive_from(frame + 5, size - 5, DATA_FRAME_ORIGIN_BLE);
    process(DATA_FRAME_ORIGIN_UNKNOWN, 1);
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_BLE);
    data_frame_disconnect(DATA_FRAME_ORIGIN_USB); /* other transport unaffected */
    process(DATA_FRAME_ORIGIN_BLE, 2);
    /* Disconnect drops both partial and fully queued old-session commands. */
    data_frame_receive_from(frame, 5, DATA_FRAME_ORIGIN_USB);
    data_frame_disconnect(DATA_FRAME_ORIGIN_USB);
    data_frame_receive_from(frame + 5, size - 5, DATA_FRAME_ORIGIN_USB);
    process(DATA_FRAME_ORIGIN_UNKNOWN, 2);
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_USB);
    data_frame_disconnect(DATA_FRAME_ORIGIN_USB);
    process(DATA_FRAME_ORIGIN_UNKNOWN, 2);
    data_frame_receive(frame, size);
    process(DATA_FRAME_ORIGIN_UNKNOWN, 3);
    frame[size - 1] ^= 1;
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_USB);
    process(DATA_FRAME_ORIGIN_UNKNOWN, 3);
    frame[size - 1] ^= 1;
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_USB);
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_BLE); /* queued origin stable */
    process(DATA_FRAME_ORIGIN_USB, 4);
    /* IRQ immediately before dispatch acquisition may replace queued frame. */
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_USB);
    on_enter = replace_usb;
    process(DATA_FRAME_ORIGIN_BLE, 5);
    /* IRQ immediately after acquisition cannot replace snapshot or payload. */
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_USB);
    on_exit = replace_usb;
    process(DATA_FRAME_ORIGIN_USB, 6);
    /* Reset publishes all state atomically before admitting new ingress. */
    data_frame_receive_from(frame, 5, DATA_FRAME_ORIGIN_USB);
    on_exit = receive_ble;
    data_frame_reset();
    process(DATA_FRAME_ORIGIN_BLE, 7);
    /* BLE IRQ overlaps main-context USB receive: buffer remains USB-owned. */
    on_exit = receive_ble;
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_USB);
    process(DATA_FRAME_ORIGIN_USB, 8);
    /* Disconnect during receive defers reset until owner releases buffer. */
    on_exit = disconnect_usb;
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_USB);
    process(DATA_FRAME_ORIGIN_UNKNOWN, 8);
    data_frame_receive_from(frame, size, DATA_FRAME_ORIGIN_BLE);
    process(DATA_FRAME_ORIGIN_BLE, 9);
    return 0;
}
''')
            binary = work / "test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(work), "-I", str(ROOT / "application/src/utils"),
                str(ROOT / "application/src/utils/dataframe.c"), str(harness),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
