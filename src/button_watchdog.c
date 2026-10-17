/*
 * Mouse button watchdog.
 *
 * Rare field bug: over a congested Bluetooth link the left button stayed
 * pressed forever. ZMK reference-counts mouse buttons, so one lost or doubled
 * press/release event leaves the count above zero and the button can never be
 * released again until reboot.
 *
 * Every WD_PERIOD_MS this checks the physical switches (the kscan GPIOs).
 * Once every switch has read released on two checks in a row:
 *   - any mouse button still set in the HID report, and not held on purpose
 *     by &mtog, is force-released (count drained to zero);
 *   - the report is sent once more, so a host that missed the release over
 *     Bluetooth gets the correct state again.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zmk/hid.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KSCAN_NODE   DT_NODELABEL(kscan0)
#define WD_PERIOD_MS 250

#if !DT_NODE_HAS_PROP(KSCAN_NODE, input_gpios)
#error "button watchdog needs a direct kscan labelled kscan0 with input-gpios"
#endif

#define WD_PIN(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),
static const struct gpio_dt_spec switches[] = {
    DT_FOREACH_PROP_ELEM(KSCAN_NODE, input_gpios, WD_PIN)
};

/* buttons &mtog holds on purpose; 0 when that behavior isn't built */
__weak uint32_t mtog_held_mask(void) { return 0; }

static uint8_t released_checks;
static bool resynced;

static bool any_switch_pressed(void) {
    for (size_t i = 0; i < ARRAY_SIZE(switches); i++) {
        if (gpio_pin_get_dt(&switches[i]) > 0) {
            return true;
        }
    }
    return false;
}

static void wd_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wd_work, wd_cb);

static void wd_cb(struct k_work *work) {
    ARG_UNUSED(work);

    if (any_switch_pressed()) {
        released_checks = 0;
        resynced = false;
    } else if (released_checks < 2) {
        released_checks++;
    }

    if (released_checks >= 2) {
        uint8_t held = zmk_hid_get_mouse_report()->body.buttons;
        uint8_t stuck = held & ~(uint8_t)mtog_held_mask();

        if (stuck) {
            for (int b = 0; b < ZMK_HID_MOUSE_NUM_BUTTONS; b++) {
                if (stuck & BIT(b)) {
                    int n = 0;
                    while (n < 16 && zmk_hid_mouse_button_release(b) == 0 &&
                           (zmk_hid_get_mouse_report()->body.buttons & BIT(b))) {
                        n++;
                    }
                }
            }
            LOG_WRN("button watchdog: force-released stuck buttons 0x%02x", stuck);
            resynced = false;
        }
        if (!resynced) {
            zmk_endpoint_send_mouse_report();
            resynced = true;
        }
    }

    k_work_reschedule(&wd_work, K_MSEC(WD_PERIOD_MS));
}

static int button_watchdog_init(void) {
    k_work_reschedule(&wd_work, K_MSEC(1000));
    return 0;
}

SYS_INIT(button_watchdog_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
