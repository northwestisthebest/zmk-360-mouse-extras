# zmk-led-indicator

A binary LED indicator for ZMK that owns a GPIO directly.

Written because ZMK's backlight subsystem is the wrong tool for an indicator
LED: brightness is clamped to a minimum of 1 (no true off), the state persists
to NVS and fights the firmware default, and the brightness steps are coarse.

## What it does

The LED is lit when *either*:

- the configured layer is active (e.g. a Bluetooth layer), or
- the watched pointing device reported motion within `activity-hold-ms`.

No keymap bindings are required — the layer half hooks ZMK's
`zmk_layer_state_changed` event, so entering or leaving the layer by any means
(combo, hold-tap, macro, `&to`) drives the LED automatically.

## Devicetree

```dts
/ {
    led_indicator: led_indicator {
        compatible = "zmk,led-indicator";
        gpios = <&gpio1 0 GPIO_ACTIVE_HIGH>;
        indicate-layer = <1>;
        activity-device = <&trackball>;
        activity-hold-ms = <60>;
    };
};
```

Use `GPIO_ACTIVE_LOW` instead if the LED's cathode is on the pin.
Omit `activity-device` to disable the motion blink and keep only the layer
indicator.

## Kconfig

```conf
CONFIG_ZMK_LED_INDICATOR=y
```

## Building

As a local module:

```sh
west build -p -b nice_nano -- \
    -DSHIELD=360_mouse \
    -DZEPHYR_EXTRA_MODULES=/path/to/zmk-led-indicator
```

Or add it to `west.yml` once it lives in a git repo.
