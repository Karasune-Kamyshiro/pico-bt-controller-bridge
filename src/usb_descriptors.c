#include <string.h>
#include "tusb.h"

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    // Frei gewaehlte VID/PID (Raspberry Pi Foundation VID + eigene PID) -
    // rein lokal genutzt, keine offizielle Registrierung noetig.
    .idVendor = 0x2E8A,
    .idProduct = 0x8A01,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptoren - je EIN Gamepad pro Interface (4 Controller)
//--------------------------------------------------------------------+
uint8_t const desc_hid_report_0[] = { TUD_HID_REPORT_DESC_GAMEPAD() };
uint8_t const desc_hid_report_1[] = { TUD_HID_REPORT_DESC_GAMEPAD() };
uint8_t const desc_hid_report_2[] = { TUD_HID_REPORT_DESC_GAMEPAD() };
uint8_t const desc_hid_report_3[] = { TUD_HID_REPORT_DESC_GAMEPAD() };

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    switch (instance) {
        case 0: return desc_hid_report_0;
        case 1: return desc_hid_report_1;
        case 2: return desc_hid_report_2;
        default: return desc_hid_report_3;
    }
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum {
    ITF_NUM_HID_0,
    ITF_NUM_HID_1,
    ITF_NUM_HID_2,
    ITF_NUM_HID_3,
    ITF_NUM_TOTAL
};

#define EPNUM_HID_0 0x83
#define EPNUM_HID_1 0x84
#define EPNUM_HID_2 0x85
#define EPNUM_HID_3 0x86

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN * 4)

uint8_t desc_configuration[] = {
    // Config: total interfaces, string index, total length, attribute
    // (Remote Wakeup gesetzt!), max power (mA)
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                           TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    TUD_HID_DESCRIPTOR(ITF_NUM_HID_0, 0, HID_ITF_PROTOCOL_NONE,
                        sizeof(desc_hid_report_0), EPNUM_HID_0, CFG_TUD_HID_EP_BUFSIZE, 4),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_1, 0, HID_ITF_PROTOCOL_NONE,
                        sizeof(desc_hid_report_1), EPNUM_HID_1, CFG_TUD_HID_EP_BUFSIZE, 4),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_2, 0, HID_ITF_PROTOCOL_NONE,
                        sizeof(desc_hid_report_2), EPNUM_HID_2, CFG_TUD_HID_EP_BUFSIZE, 4),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_3, 0, HID_ITF_PROTOCOL_NONE,
                        sizeof(desc_hid_report_3), EPNUM_HID_3, CFG_TUD_HID_EP_BUFSIZE, 4),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// Patcht bNumInterfaces + wTotalLength im Config-Descriptor auf n aktive
// Interfaces. Interface 0 bleibt immer erhalten (USB-Remote-Wakeup
// unabhaengig von Controllern). MUSS zusammen mit einem vollen
// tud_deinit()/tud_init()-Zyklus (siehe usb_hid.c) aufgerufen werden -
// tud_disconnect()/tud_connect() allein reicht NICHT (siehe Chat: TinyUSBs
// interne Interface-Buchhaltung wird dabei nicht neu aufgebaut).
void usb_descriptors_set_active_interfaces(uint8_t n) {
    if (n < 1) n = 1;
    if (n > ITF_NUM_TOTAL) n = ITF_NUM_TOTAL;

    uint16_t total_len = TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN * n;
    desc_configuration[2] = (uint8_t)(total_len & 0xFF);
    desc_configuration[3] = (uint8_t)((total_len >> 8) & 0xFF);
    desc_configuration[4] = n;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: Sprache (Englisch/US)
    "Pico",                     // 1: Manufacturer
    "BT Controller Bridge",     // 2: Product (lsusb zeigt Manufacturer+Product zusammen)
    "0001",                     // 3: Serial
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;

        const char* str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
