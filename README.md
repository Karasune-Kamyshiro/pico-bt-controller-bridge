# Pico BT Controller

Turns a Raspberry Pi Pico 2 W into a standalone Bluetooth-to-USB bridge for
gamepads (DualSense, DualShock, Xbox Wireless, 8BitDo, generic HID controllers, ...).

Supports up to **4 simultaneous controllers**, each showing up on the PC as its own
USB gamepad (`/dev/input/js0` .. `js3`). No driver, no background process needed on
the PC side — the Pico handles everything on its own.

## Features

- Up to 4 simultaneous Bluetooth controllers, each exposed as an independent USB HID
  gamepad
- **USB Remote Wakeup**: wakes the PC from suspend as soon as an already-paired
  controller reconnects (e.g. via a button press)
- Controllers automatically disconnect once the PC goes into suspend (saves battery,
  ensures a clean reconnect on the next wake)
- Player LED on the controller reflects its assigned player slot (if supported by the
  controller driver, e.g. DualSense, 8BitDo SN30 Pro)
- Pairing mode can be reopened at any time via a button combo (hold Start+Select for
  3s), even while other controllers are already connected — no reboot needed
- Dynamic USB interface count: only as many USB interfaces are exposed as controllers
  are actually connected (instead of always exposing 4)
- Status LED: blinks while searching for controllers, stays solid once at least one
  is connected

## Hardware

- Raspberry Pi Pico 2 W (RP2350, onboard WiFi/BT chip required)
- USB cable to the PC

## Requirements

- **Pico SDK 2.1.1** (not `master`/newer versions!) — newer Pico SDK releases bundle a
  BTstack version with breaking API changes relative to the Bluepad32 version used
  here (`hids_client_*` was renamed to `hids_host_*`). This project is tested and
  works reliably with Pico SDK 2.1.1.
- ARM GNU Toolchain (`arm-none-eabi-gcc` + `gdb`)
- CMake, Python 3

```bash
git clone https://github.com/raspberrypi/pico-sdk.git --branch 2.1.1 pico-sdk-2.1.1
cd pico-sdk-2.1.1
git submodule update --init --recursive
```

If building with a very recent host compiler (GCC 15+) fails inside the `pioasm`
build tool (missing `<cstdint>` includes), apply this patch:

```bash
sed -i '1i #include <cstdint>' pico-sdk-2.1.1/tools/pioasm/output_format.h
sed -i '1i #include <cstdint>' pico-sdk-2.1.1/tools/pioasm/pio_types.h
```

## Building

```bash
git clone --recursive https://github.com/<your-username>/pico-bt-controller.git
cd pico-bt-controller
mkdir build && cd build
cmake -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=/path/to/pico-sdk-2.1.1 ..
make -j$(nproc)
```

Produces `pico_bt_controller.uf2`.

## Flashing

Connect the Pico via USB while holding **BOOTSEL** (it mounts as a mass storage
device named `RPI-RP2`), then:

```bash
cp pico_bt_controller.uf2 /run/media/$USER/RPI-RP2/
```

The Pico reboots automatically.

## Usage

1. Plug the Pico into the PC (leave it connected permanently)
2. Put a controller into Bluetooth pairing mode — the Pico is discoverable on first
   boot and connects automatically
3. After that, a single button press on the controller (e.g. the PS button)
   reconnects it — this also wakes the PC from suspend if needed
4. **To open pairing mode again** (e.g. to add another controller while others are
   already connected): hold **Start + Select for 3 seconds** on any connected
   controller. The Pico's LED blinks to confirm.

## Known limitations

- Analog triggers (L2/R2) are reported both as an axis and as a digital button —
  redundant for apps that only expect one or the other, but not a bug
- Player LED control depends on the individual controller driver inside Bluepad32 —
  not every supported controller implements it (e.g. the GuliKit KK3 Max currently
  always shows "Player 1")
- Dynamically changing the USB interface count causes a brief (~0.5s) USB reconnect
  blip for all controllers whenever another one connects or disconnects, due to a
  full `tud_deinit()`/`tud_init()` cycle

## Credits

- [Bluepad32](https://github.com/ricardoquesada/bluepad32) by Ricardo Quesada — the
  Bluetooth host library this project is built on
- [TinyUSB](https://github.com/hathach/tinyusb) for the USB HID stack

## License

Own code: [MIT](LICENSE) (or adjust as preferred)
Bluepad32 (included as a submodule): Apache 2.0
