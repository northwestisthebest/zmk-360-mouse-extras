# zmk-360-mouse-extras

A ZMK module with the pieces the [360 Mouse](https://github.com/northwestisthebest/360-zmk-mouse)
needs that ZMK doesn't provide on its own. Each part is independent and
switches on only when its devicetree node exists, so it can be reused in other
ZMK pointing devices.

| Part | Devicetree `compatible` | What it does |
|---|---|---|
| [LED indicator](#led-indicator) | `zmk,led-indicator` | One GPIO LED: layer state, pointer motion, mode blinks, held-button light |
| [Mouse button toggle](#mouse-button-toggle-mtog) | `zmk,behavior-mouse-toggle` | `&mtog`: press = button held, press again = released |
| [Button watchdog](#button-watchdog) | — (Kconfig) | Releases mouse buttons left stuck after every switch is up |
| [BLE report throttle](#ble-report-throttle) | `zmk,input-processor-ble-throttle` | Caps pointer/scroll reports over Bluetooth (default 125/s); USB untouched |

Tested on ZMK with Zephyr 4.1 (`nice_nano` / nRF52840).

---

## Installation

Add the module to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: northwestisthebest
      url-base: https://github.com/northwestisthebest
  projects:
    - name: zmk-360-mouse-extras
      remote: northwestisthebest
      revision: main
```

Then `west update`. Don't pass it with `-DZEPHYR_EXTRA_MODULES` on the command
line: that overrides ZMK's own module list and drops the board definitions.

---

## LED indicator

A binary LED that owns its GPIO directly.

ZMK's backlight subsystem is the wrong tool for an indicator: brightness is
clamped to at least 1 (no real off), its state is saved to flash and overrides
the firmware default after boot, and its steps are coarse.

The LED is lit when **any** of these is true:

- the `indicate-layer` is active (e.g. a Bluetooth menu layer);
- the `activity-device` reported motion within `activity-hold-ms`;
- a button is held by `&mtog`.

Layers listed in `blink-layers` announce themselves instead of lighting
steadily: entering the first listed layer blinks **twice**, the second
**three times**, and so on; leaving any of them blinks **once**.

No keymap bindings are needed. The module listens to ZMK's layer events, so
the LED follows a layer however it is entered (combo, hold-tap, macro, `&to`).

```dts
/ {
    led_indicator: led_indicator {
        compatible = "zmk,led-indicator";
        gpios = <&gpio1 0 GPIO_ACTIVE_HIGH>;
        indicate-layer = <1>;           /* steady while layer 1 is on */
        blink-layers = <2 3>;           /* layer 2: 2 blinks, layer 3: 3 blinks */
        activity-device = <&trackball>; /* flicker on motion */
        activity-hold-ms = <60>;
    };
};
```

| Property | Default | |
|---|---|---|
| `gpios` | required | Use `GPIO_ACTIVE_LOW` if the pin sinks the LED |
| `indicate-layer` | 1 | Layer that lights the LED steadily |
| `blink-layers` | none | Mode layers announced by blinking |
| `activity-device` | none | Pointing device to watch; omit to disable motion flicker |
| `activity-hold-ms` | 60 | How long the LED stays on after the last motion |

Kconfig: `CONFIG_ZMK_LED_INDICATOR` (on automatically when the node exists).

---

## Mouse button toggle (`&mtog`)

Each press flips the button between held and released. Key release is
ignored. Typical use: bind it in place of a normal button on a mode layer, so
that button becomes "click to hold, click to release" — dragging, hold-to-aim,
rotating a 3D view.

```dts
/ {
    behaviors {
        mtog: mouse_toggle {
            compatible = "zmk,behavior-mouse-toggle";
            #binding-cells = <1>;
        };
    };

    keymap {
        compatible = "zmk,keymap";
        default_layer  { bindings = <&mkp LCLK &mkp MCLK &mkp RCLK>; };
        rmb_lock_layer { bindings = <&trans &trans &mtog RCLK>; };
    };
};
```

Behaviour worth knowing:

- **Turning off any layer releases whatever `&mtog` holds**, so leaving the
  mode can't leave a button stuck.
- ZMK counts mouse-button presses and releases a button only when the count
  reaches zero. While `&mtog` holds a button, normal clicks of the same button
  are absorbed and the button stays down. Only `&mtog` releases it.
- It reports through ZMK's own `&mkp` device, so `&mkp` must be used somewhere
  in the keymap.
- The LED indicator lights while a button is held.

Kconfig: `CONFIG_ZMK_BEHAVIOR_MOUSE_TOGGLE` (on automatically when the node exists).

---

## Button watchdog

ZMK tracks buttons as press/release **events** and counts presses per button.
If a release event is ever lost, the count stays above zero and the button
remains pressed until reboot, on every host and after switching profiles. This
was seen in practice over a weak Bluetooth link, where ZMK's 4-entry key-scan
queue overflowed while the firmware was blocked on a full Bluetooth send queue.

The watchdog does what a simple Arduino mouse does: it trusts the physical
switches. Every 250 ms it reads the key-scan GPIOs directly. Once **all**
switches have read released twice in a row:

- any mouse button still set in the HID report is force-released, except
  buttons held on purpose by `&mtog`;
- the current report is sent once more, so a host that missed a release
  recovers too.

A button held down by your finger is never touched. A stuck button clears
within about 0.5 s of letting go. Each forced release logs
`button watchdog: force-released stuck buttons 0x..`.

Requirements and caveats:

- needs a direct key scan labelled `kscan0` with `input-gpios`;
- anything that holds a mouse button **without** a physical switch held must be
  excluded the way `&mtog` is, or it will be released after half a second.

Kconfig: `CONFIG_ZMK_MOUSE_BUTTON_WATCHDOG` (default on with the LED indicator).

Also recommended, as the actual fix for the lost events:

```conf
CONFIG_ZMK_KSCAN_EVENT_QUEUE_SIZE=32
```

---

## BLE report throttle

ZMK sends one Bluetooth notification per pointer report and queues at most 20.
A sensor at 250 Hz on a weak link fills that queue in about 80 ms; after that
every new report blocks the firmware for up to 100 ms. The result is visible
stutter, and stalls long enough to drop button events.

This input processor merges reports while the output is Bluetooth, so at most
one goes out per `period-ms`. Movement is **added up, not dropped**: a sensor
reading arrives as "X moved", then "Y moved, send now". The throttle removes
"send now" when the previous report was less than `period-ms` ago, so ZMK
carries the motion into the next report. A short timer sends the remainder if
movement stops in between. Extra latency is at most one sensor period (4 ms at
250 Hz), below the 7.5–15 ms Bluetooth connection interval.

- **USB:** no effect; the full sensor rate goes out.
- **Buttons:** never delayed.
- Works for any relative input: pointer and scroll (e.g. a joystick driver).

```dts
/ {
    ble_throttle: ble_throttle {
        compatible = "zmk,input-processor-ble-throttle";
        #input-processor-cells = <0>;
        period-ms = <8>;                /* 125 reports/s over Bluetooth */
    };

    trackball_listener {
        compatible = "zmk,input-listener";
        device = <&trackball>;
        input-processors = <&ble_throttle>;
    };
};
```

Measured on the 360 Mouse: up to 240 reports/s over USB, up to 120/s over
Bluetooth.

Kconfig: `CONFIG_ZMK_INPUT_PROCESSOR_BLE_THROTTLE` (on automatically when the node exists).

---

## License

MIT
