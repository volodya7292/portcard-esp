#include "usb.h"
#include "tusb.h"
#include "esp_private/usb_phy.h"


  #define EPNUM_AUDIO_IN    0x01
  #define EPNUM_AUDIO_OUT   0x01

  #define EPNUM_CDC_NOTIF   0x83
  #define EPNUM_CDC_OUT     0x04
  #define EPNUM_CDC_IN      0x84

#define EPNUM_0_CDC_NOTIF 0x81

#define USBD_CDC_EP_CMD (0x81)
#define USBD_CDC_CMD_MAX_SIZE (8)
#define USBD_CDC_IN_OUT_MAX_SIZE (64)

// #define CONFIG_TOTAL_LEN    	(TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO * TUD_AUDIO_CONVERTER_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN)
#define CONFIG_TOTAL_LEN    	(TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO * TUD_AUDIO_CONVERTER_DESC_LEN)
// #define CONFIG_TOTAL_LEN    	(TUD_CONFIG_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN)

#define USBD_STACK_SIZE     4096
StackType_t  usb_device_stack[USBD_STACK_SIZE];
StaticTask_t usb_device_taskdef;


const char* const serial = "X3GF35JWE452FSEW";

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]     AUDIO | MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
    _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VENDOR, 5) )

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Use Interface Association Descriptor (IAD) for Audio
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCafe,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const desc_configuration[CONFIG_TOTAL_LEN] =
{
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 200),

    TUD_AUDIO_CONVERTER_DESCRIPTOR(2, EPNUM_AUDIO_OUT),

    // TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF,
    //     USBD_CDC_CMD_MAX_SIZE, EPNUM_CDC_OUT, EPNUM_CDC_IN, USBD_CDC_IN_OUT_MAX_SIZE),


    // TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_TOTAL_LEN, 0x00, 200),
    // // // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    // // TUD_CDC_DESCRIPTOR(0, 5, 0x80 | 1, 8, 2, 0x80 | 2, 64),
    // TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF,
    //     USBD_CDC_CMD_MAX_SIZE, EPNUM_CDC_OUT, EPNUM_CDC_IN, USBD_CDC_IN_OUT_MAX_SIZE),

};

#define STR_DESC_COUNT 6
char const* string_desc_arr [STR_DESC_COUNT] =
{
    (const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
    "volodya7292",                 // 1: Manufacturer
    "portcard",                    // 2: Product
    serial,                        // 3: Serials, uses the flash ID
    "PortCard",                    // 4: Audio Interface
    "CDC",                         // 5
};

// USB Device Driver task
// This top level thread process all usb events and invoke callbacks
void usb_device_task(void *param)
{
    (void) param;

    // This should be called after scheduler/kernel is started.
    // Otherwise it could cause kernel issue since USB IRQ handler does use RTOS queue API.
    tusb_init();

    // RTOS forever loop
    while (1) {
        // tinyusb device task
        tud_task();
    }
}

static usb_phy_handle_t phy_hdl;
static void usb_phy_init(void)
{
    // Configure USB PHY
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_FULL,
    };
    usb_new_phy(&phy_conf, &phy_hdl);
}

void init_usb() {
    usb_phy_init();

    // const tinyusb_config_t tusb_cfg = {
    //     .device_descriptor = &desc_device,
    //     .string_descriptor = string_desc_arr,
    //     .string_descriptor_count = STR_DESC_COUNT,
    //     .external_phy = false, // In the most cases you need to use a `false` value
    //     .configuration_descriptor = desc_configuration,
    // };
    // const tinyusb_config_t tusb_cfg = {
    //     .device_descriptor = NULL,
    //     .string_descriptor = NULL,
    //     .string_descriptor_count = 0,
    //     .external_phy = false, // In the most cases you need to use a `false` value
    //     .configuration_descriptor = NULL,
    // };

    // ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    // tinyusb_config_cdcacm_t acm_cfg = { 0 }; // the configuration uses default values
    // ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    // esp_tusb_init_console(TINYUSB_CDC_ACM_0); // log to usb

     (void) xTaskCreateStatic(usb_device_task, "usbd", USBD_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, usb_device_stack, &usb_device_taskdef);
}

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index; // for multiple configurations
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// String Descriptor Index
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};


static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;

    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        // if (index == 3) {
        //     pico_get_unique_board_id_string(serial, sizeof(serial));
        // }

        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        const char* str = string_desc_arr[index];

        // Cap at max char
        chr_count = strlen(str);
        if (chr_count > 31) {
            chr_count = 31;
        }

        // Convert ASCII string into UTF-16
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);

    return _desc_str;
}