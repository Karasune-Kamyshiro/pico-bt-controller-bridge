#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include <stdbool.h>

// Eigenes, minimales Struct statt TinyUSBs hid_gamepad_report_t direkt zu
// nutzen - deren Header (class/hid/hid.h) kollidiert mit BTstacks eigenem
// btstack_hid.h (beide definieren hid_report_type_t), wenn beide Header in
// derselben Datei landen wie in my_platform.c (das uni.h -> btstack.h
// -> btstack_hid.h einbindet). Layout ist absichtlich identisch zu
// hid_gamepad_report_t, wird in usb_hid.c 1:1 uebernommen.
typedef struct {
    int8_t x, y, z, rz, rx, ry;
    uint8_t hat;
    uint32_t buttons;
} usb_gamepad_report_t;

#define USB_HID_MAX_PLAYERS 4

// Aus usb_descriptors.c - patcht den Config-Descriptor auf n aktive Interfaces
void usb_descriptors_set_active_interfaces(uint8_t n);

// Startet Core 1 (TinyUSB-Loop). Einmalig aus my_platform_init() aufrufen.
void usb_hid_init(void);

// player_index: 0 oder 1 - steuert, ueber welches der zwei USB-HID-
// Interfaces (siehe usb_descriptors.c) der Report rausgeht.
void usb_hid_update_gamepad(uint8_t player_index, const usb_gamepad_report_t* report);

// Von on_device_connected()/on_device_disconnected() aufrufen.
// notify_connected loest bei Bedarf auch das USB-Remote-Wakeup aus.
void usb_hid_notify_connected(void);
void usb_hid_notify_disconnected(void);

// Fuer LED-Feedback von Core 0 aus (cyw43/LED darf nur von Core 0 angesprochen
// werden, siehe Begruendung im Chat) - liefert true wenn mind. 1 Controller verbunden.
bool usb_hid_is_connected(void);

// Von Core 0 periodisch abfragen (z.B. im LED-Timer): liefert true GENAU
// EINMAL, wenn der PC seit dem letzten Aufruf in Suspend gegangen ist.
bool usb_hid_consume_suspend_event(void);

#endif
