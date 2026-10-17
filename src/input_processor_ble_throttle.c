/*
 * BLE report-rate throttle (input processor).
 *
 * Over USB nothing changes. Over Bluetooth, pointer and scroll reports from a
 * listener are merged so at most one goes out per period-ms (default 8 ms,
 * i.e. 125 reports/s). Nothing is dropped: the listener keeps adding motion
 * up until the report that is let through, and a short timer sends the
 * remainder if movement stops in between.
 *
 * Why: ZMK sends one BLE notification per report and queues at most 20. At
 * 250 Hz a weak link backs that queue up within ~80 ms, after which every new
 * report blocks for up to 100 ms - visible stutter, and stalls that can drop
 * button events.
 *
 * Buttons (EV_KEY) are never delayed.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_ble_throttle

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <drivers/input_processor.h>

#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/endpoints_types.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define THR_SLOTS 4 /* one per input listener using the processor */

struct thr_slot {
    const struct device *src;
    int64_t last_sync;
    bool pending;
    struct k_work_delayable flush;
};

struct thr_config {
    uint16_t period_ms;
};

struct thr_data {
    struct thr_slot slots[THR_SLOTS];
};

/* until ZMK reports the endpoint, assume Bluetooth (the cautious side) */
static bool throttle_on = true;

static void thr_flush_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct thr_slot *slot = CONTAINER_OF(dwork, struct thr_slot, flush);

    if (slot->pending && slot->src != NULL) {
        /* a zero-length event whose only job is to carry the sync */
        input_report(slot->src, INPUT_EV_REL, INPUT_REL_X, 0, true, K_NO_WAIT);
    }
}

static int thr_handle_event(const struct device *dev, struct input_event *evt, uint32_t param1,
                            uint32_t param2, struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    const struct thr_config *cfg = dev->config;
    struct thr_data *data = dev->data;

    if (!throttle_on || !evt->sync || evt->type != INPUT_EV_REL || state == NULL ||
        state->input_device_index >= THR_SLOTS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct thr_slot *slot = &data->slots[state->input_device_index];
    int64_t now = k_uptime_get();
    slot->src = evt->dev;

    if (now - slot->last_sync >= cfg->period_ms) {
        slot->last_sync = now;
        slot->pending = false;
        return ZMK_INPUT_PROC_CONTINUE; /* report goes out */
    }

    /* too soon: keep accumulating in the listener, send later */
    evt->sync = false;
    if (!slot->pending) {
        slot->pending = true;
        k_work_reschedule(&slot->flush, K_MSEC(slot->last_sync + cfg->period_ms - now));
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static int thr_endpoint_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *ev = as_zmk_endpoint_changed(eh);
    if (ev != NULL) {
        throttle_on = (ev->endpoint.transport != ZMK_TRANSPORT_USB);
        LOG_INF("report throttle %s", throttle_on ? "on (BLE)" : "off (USB)");
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(ble_throttle, thr_endpoint_listener);
ZMK_SUBSCRIPTION(ble_throttle, zmk_endpoint_changed);

static int thr_init(const struct device *dev) {
    struct thr_data *data = dev->data;
    for (int i = 0; i < THR_SLOTS; i++) {
        k_work_init_delayable(&data->slots[i].flush, thr_flush_cb);
    }
    return 0;
}

static const struct zmk_input_processor_driver_api thr_api = {
    .handle_event = thr_handle_event,
};

#define THR_INST(n)                                                                                \
    static struct thr_data thr_data_##n;                                                           \
    static const struct thr_config thr_config_##n = {                                              \
        .period_ms = DT_INST_PROP(n, period_ms),                                                   \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, thr_init, NULL, &thr_data_##n, &thr_config_##n, POST_KERNEL,          \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &thr_api);

DT_INST_FOREACH_STATUS_OKAY(THR_INST)

#endif
