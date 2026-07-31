# Pico BT Controller

Verwandelt einen Raspberry Pi Pico 2 W in eine eigenständige Bluetooth-zu-USB-Brücke für
Gamepads (DualSense, DualShock, Xbox Wireless, 8BitDo, generische HID-Controller, ...).

Bis zu **4 Controller gleichzeitig**, jeder erscheint am PC als eigenes USB-Gamepad
(`/dev/input/js0` .. `js3`). Kein Treiber, kein Hintergrundprozess auf dem PC nötig –
der Pico erledigt alles selbst.

## Features

- Bis zu 4 gleichzeitige Bluetooth-Controller, jeweils als eigenständiges USB-HID-Gamepad
- **USB Remote Wakeup**: weckt den PC aus dem Suspend, sobald ein bereits gekoppelter
  Controller sich wieder verbindet (z. B. durch Tastendruck)
- Controller trennen sich automatisch, sobald der PC in den Suspend geht (spart Akku,
  sorgt für einen sauberen Reconnect beim nächsten Aufwecken)
- Player-LED am Controller zeigt die zugewiesene Spielernummer (sofern vom
  Controller-Treiber unterstützt, z. B. DualSense, 8BitDo SN30 Pro)
- Pairing-Modus lässt sich jederzeit per Tastenkombo neu öffnen (Start+Select 3s
  halten), auch wenn schon Controller verbunden sind – kein Neustart nötig
- Dynamische USB-Interface-Anzahl: nur so viele Interfaces sichtbar wie tatsächlich
  Controller verbunden sind (statt immer fix 4)
- Status-LED: blinkt beim Suchen nach Controllern, leuchtet dauerhaft sobald mindestens
  einer verbunden ist

## Hardware

- Raspberry Pi Pico 2 W (RP2350, WLAN/BT-Chip zwingend erforderlich)
- USB-Kabel zum PC

## Voraussetzungen

- **Pico SDK 2.1.1** (nicht `master`/neuere Versionen!) – neuere Pico-SDK-Releases
  bringen eine BTstack-Version mit, die inkompatible API-Änderungen gegenüber dem hier
  verwendeten Bluepad32-Stand hat (`hids_client_*` wurde zu `hids_host_*` umbenannt).
  Mit Pico SDK 2.1.1 ist das Projekt getestet und funktioniert zuverlässig.
- ARM GNU Toolchain (`arm-none-eabi-gcc` + `gdb`)
- CMake, Python 3

```bash
git clone https://github.com/raspberrypi/pico-sdk.git --branch 2.1.1 pico-sdk-2.1.1
cd pico-sdk-2.1.1
git submodule update --init --recursive
```

Falls beim Bauen mit sehr neuen Host-Compilern (GCC 15+) Fehler im `pioasm`-Build-Tool
auftreten (fehlende `<cstdint>`-Includes), hilft folgender Patch:

```bash
sed -i '1i #include <cstdint>' pico-sdk-2.1.1/tools/pioasm/output_format.h
sed -i '1i #include <cstdint>' pico-sdk-2.1.1/tools/pioasm/pio_types.h
```

## Bauen

```bash
git clone --recursive https://github.com/Karasune-Kamyshiro/pico-bt-controller.git
cd pico-bt-controller
mkdir build && cd build
cmake -DPICO_BOARD=pico2_w -DPICO_SDK_PATH=/pfad/zu/pico-sdk-2.1.1 ..
make -j$(nproc)
```

Erzeugt `pico_bt_controller.uf2`.

## Flashen

Pico bei gedrückter **BOOTSEL**-Taste per USB anschließen (meldet sich als
Massenspeicher `RPI-RP2`), dann:

```bash
cp pico_bt_controller.uf2 /run/media/$USER/RPI-RP2/
```

Der Pico startet automatisch neu.

## Benutzung

1. Pico an den PC anschließen (bleibt dauerhaft angeschlossen)
2. Controller in den Bluetooth-Pairing-Modus bringen – der Pico ist beim ersten Start
   automatisch discoverable und verbindet sich selbstständig
3. Danach reicht am Controller ein Tastendruck (z. B. PS-Taste), um sich erneut zu
   verbinden – das weckt bei Bedarf auch den PC aus dem Suspend
4. **Neuen Pairing-Modus öffnen** (z. B. für einen weiteren Controller, wenn schon
   welche verbunden sind): am Controller **Start + Select 3 Sekunden halten**, LED
   am Pico blinkt zur Bestätigung

## Bekannte Einschränkungen

- Analoge Trigger (L2/R2) werden sowohl als Achse als auch als digitaler Button
  gemeldet – für Spiele/Anwendungen, die nur eines von beidem erwarten, kann das
  redundant wirken, ist aber kein Fehler
- Player-LED-Steuerung hängt vom jeweiligen Controller-Treiber in Bluepad32 ab –
  nicht jeder unterstützte Controller-Typ implementiert das (z. B. GuliKit KK3 Max
  zeigt aktuell immer "Player 1")
- Dynamische Interface-Umschaltung verursacht beim Verbinden/Trennen weiterer
  Controller einen kurzen (~0,5s) USB-Reconnect-Ruckler für alle Controller,
  bedingt durch einen vollständigen `tud_deinit()`/`tud_init()`-Zyklus

## Danke an

- [Bluepad32](https://github.com/ricardoquesada/bluepad32) von Ricardo Quesada –
  die eigentliche Bluetooth-Host-Bibliothek, auf der dieses Projekt aufbaut
- [TinyUSB](https://github.com/hathach/tinyusb) für den USB-HID-Stack

## Lizenz

Eigener Code: [MIT](LICENSE) (oder nach Wahl anpassen)
Bluepad32 (als Submodule eingebunden): Apache 2.0
