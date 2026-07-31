// my_platform.c
//
// Bluepad32 Custom Platform: nimmt Controller-Reports entgegen und reicht
// sie als USB-HID-Gamepad an den PC weiter (Core 1, siehe usb_hid.c).
// Beim ersten Connect wird zusaetzlich USB-Remote-Wakeup ausgeloest.

#include <uni.h>
#include "pico/cyw43_arch.h"
#include "usb_hid.h"

//--------------------------------------------------------------------+
// LED-Feedback via BTstack-Timer - MUSS auf Core 0 laufen (siehe Chat:
// cyw43/LED ist nicht multicore-sicher, daher hier statt in usb_hid.c)
//--------------------------------------------------------------------+
#define LED_BLINK_MS 250
static btstack_timer_source_t led_timer;

// Pairing-Modus-Feedback: wird ueber den bereits laufenden LED-Timer
// mit abgewickelt (kein separater Timer/sleep_ms noetig) - 6 Ticks a
// 250ms = 3 schnelle Auf/Ab-Zyklen als sichtbares Bestaetigungssignal.
static volatile int g_pairing_feedback_ticks = 0;

// Bluepad32 unterstuetzt standardmaessig bis zu 4 gleichzeitige Geraete
#define MAX_DEVICES 4

static void disconnect_all_controllers(void) {
    for (int idx = 0; idx < MAX_DEVICES; idx++) {
        uni_hid_device_t* d = uni_hid_device_get_instance_for_idx(idx);
        if (d && uni_bt_conn_is_connected(&d->conn)) {
            logi("custom: trenne Controller (PC geht in Suspend)\n");
            uni_hid_device_disconnect(d);
        }
    }
}

static void led_timer_handler(btstack_timer_source_t* ts) {
    // PC in Suspend gegangen? Alle Controller trennen (Akku sparen,
    // sauberer Reconnect+Wake beim naechsten Tastendruck)
    if (usb_hid_consume_suspend_event()) {
        disconnect_all_controllers();
    }

    static bool on = false;
    if (g_pairing_feedback_ticks > 0) {
        // Deterministisches Muster (gerade/ungerade Tick-Zahl), unabhaengig
        // vom vorherigen LED-Zustand - robuster als reines Toggle
        on = (g_pairing_feedback_ticks % 2 == 0);
        g_pairing_feedback_ticks--;
    } else if (usb_hid_is_connected()) {
        on = true;
    } else {
        on = !on;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    btstack_run_loop_set_timer(&led_timer, LED_BLINK_MS);
    btstack_run_loop_add_timer(&led_timer);
}

//--------------------------------------------------------------------+
// Hilfsfunktion: bluepad32-Dpad-Bitmask -> HID-Hat-Wert (0-7, 8=neutral)
//--------------------------------------------------------------------+
// Verhindert int8_t-Overflow (-128..127): bluepad32 liefert axis_x/y/rx/ry
// bis max. +512, /4 ergibt dann +128 - das kippt ohne Clamp auf -128 um.
static inline int8_t clamp_axis_i8(int32_t v) {
    if (v > 127) return 127;
    if (v < -127) return -127;
    return (int8_t)v;
}

static uint8_t dpad_to_hat(uint8_t dpad) {
    bool up = dpad & DPAD_UP;
    bool down = dpad & DPAD_DOWN;
    bool left = dpad & DPAD_LEFT;
    bool right = dpad & DPAD_RIGHT;

    if (up && right) return 2;   // NE
    if (right && down) return 4; // SE
    if (down && left) return 6;  // SW
    if (left && up) return 8;    // NW
    if (up) return 1;            // N
    if (right) return 3;         // E
    if (down) return 5;          // S
    if (left) return 7;          // W
    return 0;                    // neutral (ausserhalb 1-8 = "kein Dpad gedrueckt")
}

// Ordnet jedem Controller einen festen Slot (0 oder 1) zu -> steuert, ueber
// welches der zwei USB-HID-Interfaces (usb_descriptors.c) der Report geht.
static uni_hid_device_t* g_player_devices[USB_HID_MAX_PLAYERS] = {NULL, NULL};

static int assign_player_slot(uni_hid_device_t* d) {
    for (int i = 0; i < USB_HID_MAX_PLAYERS; i++) {
        if (g_player_devices[i] == d) return i;
    }
    for (int i = 0; i < USB_HID_MAX_PLAYERS; i++) {
        if (g_player_devices[i] == NULL) {
            g_player_devices[i] = d;
            return i;
        }
    }
    return -1;
}

static int find_player_slot(uni_hid_device_t* d) {
    for (int i = 0; i < USB_HID_MAX_PLAYERS; i++) {
        if (g_player_devices[i] == d) return i;
    }
    return -1;
}

static void free_player_slot(uni_hid_device_t* d) {
    for (int i = 0; i < USB_HID_MAX_PLAYERS; i++) {
        if (g_player_devices[i] == d) g_player_devices[i] = NULL;
    }
}

//--------------------------------------------------------------------+
// Platform Overrides
//--------------------------------------------------------------------+
static void my_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    logi("custom: my_platform_init()\n");

    // Boot-Signal: 3x kurz blinken, zeigt "Pico ist online und bereit"
    for (int i = 0; i < 3; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(150);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(150);
    }

    usb_hid_init(); // startet Core 1 mit der TinyUSB-Loop

    led_timer.process = &led_timer_handler;
    btstack_run_loop_set_timer(&led_timer, LED_BLINK_MS);
    btstack_run_loop_add_timer(&led_timer);
}

static void my_platform_on_init_complete(void) {
    logi("custom: on_init_complete()\n");
    uni_bt_enable_new_connections_unsafe(true);
}

static void my_platform_on_device_connected(uni_hid_device_t* d) {
    ARG_UNUSED(d);
    logi("custom: device connected\n");
}

static void my_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("custom: device disconnected\n");
    free_player_slot(d);
    usb_hid_notify_disconnected();
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t* d) {
    logi("custom: device ready\n");

    int slot = assign_player_slot(d);
    if (slot < 0) {
        logi("custom: kein freier Player-Slot mehr (max %d Controller)\n", USB_HID_MAX_PLAYERS);
        return UNI_ERROR_IGNORE_DEVICE;
    }

    // Hier ist der Controller wirklich einsatzbereit - das ist unser
    // eigentlicher Wake-Trigger, nicht schon bei on_device_connected.
    usb_hid_notify_connected();

    // DIAGNOSE: Pico-LED blinkt (slot+1) mal, um zu bestaetigen welcher
    // Slot tatsaechlich zugewiesen wurde. Kommt spaeter wieder raus.
    for (int i = 0; i <= slot; i++) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(150);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(200);
    }

    // Player-LED am Controller passend zum zugewiesenen Slot setzen
    // (1-indiziert: Slot 0 = "Player 1" usw.) - nicht jeder Controller-Typ
    // unterstuetzt das, daher der NULL-Check. Kurze Verzoegerung, da die
    // DualSense direkt zuvor noch ihre eigene Lightbar-Init-Nachricht
    // sendet - zu dicht hintereinander scheint der Player-LED-Report
    // sonst verloren zu gehen.
    if (d->report_parser.set_player_leds != NULL) {
        sleep_ms(200);
        d->report_parser.set_player_leds(d, (uint8_t)(slot + 1));
    }

    return UNI_ERROR_SUCCESS;
}

// Pairing-Modus per Tastenkombo: Start+Select 3 Sekunden halten
#define PAIR_COMBO_HOLD_MS 3000
static bool g_pair_combo_active = false;
static bool g_pair_combo_fired = false;
static absolute_time_t g_pair_combo_start;

static void check_pairing_combo(uni_controller_t* ctl) {
    bool combo_pressed = (ctl->gamepad.misc_buttons & MISC_BUTTON_START) &&
                          (ctl->gamepad.misc_buttons & MISC_BUTTON_SELECT);

    if (combo_pressed) {
        if (!g_pair_combo_active) {
            g_pair_combo_active = true;
            g_pair_combo_fired = false;
            g_pair_combo_start = get_absolute_time();
        } else if (!g_pair_combo_fired &&
                   absolute_time_diff_us(g_pair_combo_start, get_absolute_time()) >= PAIR_COMBO_HOLD_MS * 1000) {
            g_pair_combo_fired = true;
            logi("custom: Pairing-Modus per Tastenkombo aktiviert\n");
            uni_bt_enable_new_connections_unsafe(true);
            g_pairing_feedback_ticks = 10; // 2,5s deutlich sichtbares Blinken via LED-Timer
        }
    } else {
        g_pair_combo_active = false;
        g_pair_combo_fired = false;
    }
}

static void my_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }

    check_pairing_combo(ctl);

    int slot = find_player_slot(d);
    if (slot < 0) return; // sollte nicht passieren, aber sicherheitshalber

    usb_gamepad_report_t report = {0};

    // bluepad32: axis_x/y/rx/ry etwa -511..512, wir skalieren auf int8 (-127..127)
    report.x = clamp_axis_i8(ctl->gamepad.axis_x / 4);
    report.y = clamp_axis_i8(ctl->gamepad.axis_y / 4);
    report.rx = clamp_axis_i8(ctl->gamepad.axis_rx / 4);
    report.ry = clamp_axis_i8(ctl->gamepad.axis_ry / 4);

    // brake/throttle: 0..1023 -> 0..127
    report.z = clamp_axis_i8(ctl->gamepad.brake / 8);
    report.rz = clamp_axis_i8(ctl->gamepad.throttle / 8);

    report.hat = dpad_to_hat(ctl->gamepad.dpad);

    // Explizites Mapping ueber die von Bluepad32 (uni_hid_parser_ds5.c) bestaetigten
    // Button-Konstanten, statt die rohe Bitmask 1:1 durchzureichen - so haben wir
    // volle Kontrolle ueber die Ziel-Bit-Positionen (Xbox-aehnliche Reihenfolge).
    uint32_t b = 0;
    if (ctl->gamepad.buttons & BUTTON_A) b |= (1u << 0);
    if (ctl->gamepad.buttons & BUTTON_B) b |= (1u << 1);
    if (ctl->gamepad.buttons & BUTTON_X) b |= (1u << 2);
    if (ctl->gamepad.buttons & BUTTON_Y) b |= (1u << 3);
    if (ctl->gamepad.buttons & BUTTON_SHOULDER_L) b |= (1u << 4);  // L1
    if (ctl->gamepad.buttons & BUTTON_SHOULDER_R) b |= (1u << 5);  // R1
    if (ctl->gamepad.buttons & BUTTON_TRIGGER_L) b |= (1u << 6);   // L2 (digital)
    if (ctl->gamepad.buttons & BUTTON_TRIGGER_R) b |= (1u << 7);   // R2 (digital)
    if (ctl->gamepad.buttons & BUTTON_THUMB_L) b |= (1u << 8);     // L3
    if (ctl->gamepad.buttons & BUTTON_THUMB_R) b |= (1u << 9);     // R3
    if (ctl->gamepad.misc_buttons & MISC_BUTTON_SELECT) b |= (1u << 10); // Share
    if (ctl->gamepad.misc_buttons & MISC_BUTTON_START) b |= (1u << 11);  // Options
    if (ctl->gamepad.misc_buttons & MISC_BUTTON_SYSTEM) b |= (1u << 12); // PS
    report.buttons = b;

    usb_hid_update_gamepad(slot, &report);
}

static const uni_property_t* my_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    ARG_UNUSED(event);
    ARG_UNUSED(data);
}

struct uni_platform* get_my_platform(void) {
    static struct uni_platform plat = {
        .name = "PicoBTBridge",
        .init = my_platform_init,
        .on_init_complete = my_platform_on_init_complete,
        .on_device_connected = my_platform_on_device_connected,
        .on_device_disconnected = my_platform_on_device_disconnected,
        .on_device_ready = my_platform_on_device_ready,
        .on_oob_event = my_platform_on_oob_event,
        .on_controller_data = my_platform_on_controller_data,
        .get_property = my_platform_get_property,
    };

    return &plat;
}
