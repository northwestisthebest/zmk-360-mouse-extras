/*
 * &mtog - mouse button toggle.
 *
 * Press once: the button(s) go down and stay down. Press again: released.
 * Key release is ignored. Bind it on a mode layer in place of the normal
 * button; leaving any layer releases whatever it holds, so nothing stays stuck.
 * Reports go through ZMK's own &mkp input device. ZMK reference-counts mouse
 * buttons, so while the toggle holds a button, normal clicks of that same
 * button are absorbed (the button stays down) - only &mtog releases it.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_mouse_toggle

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if !DT_NODE_EXISTS(DT_NODELABEL(mkp))
#error "&mtog needs ZMK's &mkp behavior in the keymap"
#endif

#define MKP_DEV DEVICE_DT_GET(DT_NODELABEL(mkp))

#if IS_ENABLED(CONFIG_ZMK_LED_INDICATOR)
void led_indicator_set_hold(bool on);
#else
static inline void led_indicator_set_hold(bool on) { ARG_UNUSED(on); }
#endif

static uint32_t toggled;    /* bitmask of buttons we are holding */

static void report_buttons(uint32_t mask, bool pressed) {
    for (int i = 0; i < ZMK_HID_MOUSE_NUM_BUTTONS; i++) {
        if (mask & BIT(i)) {
            mask &= ~BIT(i);
            input_report_key(MKP_DEV, INPUT_BTN_0 + i, pressed ? 1 : 0, mask == 0, K_FOREVER);
        }
    }
}

static int mtog_pressed(struct zmk_behavior_binding *binding,
                        struct zmk_behavior_binding_event event) {
    uint32_t mask = binding->param1;
    bool press = (toggled & mask) != mask;   /* any not held -> hold all */

    if (press) {
        toggled |= mask;
    } else {
        toggled &= ~mask;
    }
    report_buttons(mask, press);
    led_indicator_set_hold(toggled != 0);
    LOG_DBG("mtog 0x%02x -> %s", mask, press ? "held" : "released");
    return ZMK_BEHAVIOR_OPAQUE;
}

static int mtog_released(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}


/* leaving a layer (e.g. the RMB-toggle mode) releases anything still held */
static int mtog_layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev != NULL && !ev->state && toggled) {
        report_buttons(toggled, false);
        toggled = 0;
        led_indicator_set_hold(false);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(mouse_toggle, mtog_layer_listener);
ZMK_SUBSCRIPTION(mouse_toggle, zmk_layer_state_changed);

static const struct behavior_driver_api mtog_driver_api = {
    .binding_pressed = mtog_pressed,
    .binding_released = mtog_released,
};

#define MTOG_INST(n)                                                                               \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mtog_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MTOG_INST)

#endif
