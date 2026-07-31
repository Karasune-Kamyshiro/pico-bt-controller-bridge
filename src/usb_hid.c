#include <string.h>
#include "pico/multicore.h"
#include "pico/critical_section.h"
#include "tusb.h"
#include "usb_hid.h"

static critical_section_t state_lock;
static usb_gamepad_report_t shared_report[USB_HID_MAX_PLAYERS];
static volatile bool report_dirty[USB_HID_MAX_PLAYERS] = {false, false};
static volatile bool wake_requested = false;
static volatile bool suspend_event = false;
static volatile uint8_t connected_count = 0;
static volatile uint8_t desired_active_interfaces = 1; // min. 1, fuer Remote-Wakeup
static absolute_time_t last_connect_time;

// TinyUSB ruft das automatisch auf, wenn der PC-Host in Suspend geht -
// das ist unser Signal, alle Controller aktiv zu trennen (Akku sparen +
// sauberer Reconnect beim naechsten Tastendruck).
void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    critical_section_enter_blocking(&state_lock);
    suspend_event = true;
    critical_section_exit(&state_lock);
}

void tud_resume_cb(void) {
    // Aktuell nichts zu tun - Reconnect passiert ohnehin ueber den
    // naechsten Tastendruck am Controller.
}

static void core1_entry(void) {
    // Vor der ersten Enumeration auf 1 aktives Interface patchen (Remote-
    // Wakeup bleibt so von Anfang an moeglich, auch ohne Controller)
    usb_descriptors_set_active_interfaces(1);
    uint8_t applied_interfaces = 1;

    tusb_init();

    absolute_time_t next_report_time = get_absolute_time();

    while (true) {
        tud_task();

        // Interface-Anzahl geaendert (Controller dazu/weg)? Kompletten
        // USB-Stack neu initialisieren (nicht nur disconnect/connect - das
        // baut TinyUSBs interne Interface-Buchhaltung wirklich neu auf,
        // siehe Chat/TinyUSB-Forum-Empfehlung).
        uint8_t desired = desired_active_interfaces;
        if (desired != applied_interfaces) {
            tud_deinit(0);
            sleep_ms(250);
            usb_descriptors_set_active_interfaces(desired);
            applied_interfaces = desired;
            tud_init(0);
            sleep_ms(50);
        }

        // Wake-Anfrage abarbeiten (nur sinnvoll, wenn USB tatsaechlich suspended ist)
        if (wake_requested) {
            if (tud_suspended()) {
                tud_remote_wakeup();
            }
            critical_section_enter_blocking(&state_lock);
            wake_requested = false;
            critical_section_exit(&state_lock);
        }

        // LED-Steuerung passiert NICHT hier - cyw43/LED ist nicht multicore-
        // sicher, siehe usb_hid_is_connected() + Aufrufer in my_platform.c

        // Reports mit max. ~250Hz raushauen, wenn sich was geaendert hat
        if (absolute_time_diff_us(get_absolute_time(), next_report_time) <= 0) {
            for (uint8_t p = 0; p < USB_HID_MAX_PLAYERS; p++) {
                if (!report_dirty[p]) continue;
                if (!tud_hid_n_ready(p)) continue;

                usb_gamepad_report_t local_copy;
                critical_section_enter_blocking(&state_lock);
                local_copy = shared_report[p];
                report_dirty[p] = false;
                critical_section_exit(&state_lock);

                // In TinyUSBs natives Report-Format konvertieren (identisches Layout)
                hid_gamepad_report_t tusb_report = {
                    .x = local_copy.x, .y = local_copy.y,
                    .z = local_copy.z, .rz = local_copy.rz,
                    .rx = local_copy.rx, .ry = local_copy.ry,
                    .hat = local_copy.hat, .buttons = local_copy.buttons,
                };
                // Instanz p = eigenes USB-Interface, keine Report-ID noetig (0)
                tud_hid_n_report(p, 0, &tusb_report, sizeof(tusb_report));
            }
            next_report_time = make_timeout_time_ms(4);
        }
    }
}

void usb_hid_init(void) {
    critical_section_init(&state_lock);
    memset(shared_report, 0, sizeof(shared_report));
    multicore_launch_core1(core1_entry);
}

void usb_hid_update_gamepad(uint8_t player_index, const usb_gamepad_report_t* report) {
    if (player_index >= USB_HID_MAX_PLAYERS) return;
    critical_section_enter_blocking(&state_lock);
    shared_report[player_index] = *report;
    report_dirty[player_index] = true;
    critical_section_exit(&state_lock);
}

void usb_hid_notify_connected(void) {
    critical_section_enter_blocking(&state_lock);
    connected_count++;
    wake_requested = true;
    last_connect_time = get_absolute_time();
    desired_active_interfaces = (connected_count > USB_HID_MAX_PLAYERS) ? USB_HID_MAX_PLAYERS : connected_count;
    if (desired_active_interfaces < 1) desired_active_interfaces = 1;
    critical_section_exit(&state_lock);
}

void usb_hid_notify_disconnected(void) {
    critical_section_enter_blocking(&state_lock);
    if (connected_count > 0) connected_count--;
    desired_active_interfaces = (connected_count > USB_HID_MAX_PLAYERS) ? USB_HID_MAX_PLAYERS : connected_count;
    if (desired_active_interfaces < 1) desired_active_interfaces = 1;
    critical_section_exit(&state_lock);
}

bool usb_hid_is_connected(void) {
    critical_section_enter_blocking(&state_lock);
    bool result = connected_count > 0;
    critical_section_exit(&state_lock);
    return result;
}

// Liefert true GENAU EINMAL nach einem Suspend-Event, danach wird das Flag
// zurueckgesetzt ("consume"). Von Core 0 aus periodisch abfragen.
// Ignoriert Suspend-Events innerhalb einer kurzen Schonfrist nach dem letzten
// Connect - direkt nach einem Wecken kann der USB-Bus kurzzeitig nochmal
// "suspended" wirken, das ist Rauschen im Resume-Handshake, kein echtes
// erneutes Einschlafen des PCs.
#define SUSPEND_GRACE_PERIOD_MS 5000

bool usb_hid_consume_suspend_event(void) {
    critical_section_enter_blocking(&state_lock);
    bool result = suspend_event;
    suspend_event = false;
    int64_t since_connect_ms = absolute_time_diff_us(last_connect_time, get_absolute_time()) / 1000;
    critical_section_exit(&state_lock);

    if (result && since_connect_ms < SUSPEND_GRACE_PERIOD_MS) {
        return false; // innerhalb der Schonfrist - ignorieren
    }
    return result;
}

// TinyUSB HID Callbacks (Pflicht-Implementierungen, auch wenn wir sie nicht brauchen)
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                                uint8_t* buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                            uint8_t const* buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
