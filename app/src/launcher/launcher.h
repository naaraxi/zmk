#pragma once

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/device.h>
#include <app_version.h>
#include <zmk/keymap.h>
#include "keycodes.h"
#include <zmk/matrix.h>
#include <app_version.h>
#include <zmk/sensors.h>

#define ZMK_HID_MAIN_VAL_DATA (0x00 << 0)
#define ZMK_HID_MAIN_VAL_CONST (0x01 << 0)

#define ZMK_HID_MAIN_VAL_ARRAY (0x00 << 1)
#define ZMK_HID_MAIN_VAL_VAR (0x01 << 1)

#define ZMK_HID_MAIN_VAL_ABS (0x00 << 2)
#define ZMK_HID_MAIN_VAL_REL (0x01 << 2)

#define ZMK_HID_MAIN_VAL_NO_WRAP (0x00 << 3)
#define ZMK_HID_MAIN_VAL_WRAP (0x01 << 3)

#define ZMK_HID_MAIN_VAL_LIN (0x00 << 4)
#define ZMK_HID_MAIN_VAL_NON_LIN (0x01 << 4)

#define ZMK_HID_MAIN_VAL_PREFERRED (0x00 << 5)
#define ZMK_HID_MAIN_VAL_NO_PREFERRED (0x01 << 5)

#define ZMK_HID_MAIN_VAL_NO_NULL (0x00 << 6)
#define ZMK_HID_MAIN_VAL_NULL (0x01 << 6)

#define ZMK_HID_MAIN_VAL_NON_VOL (0x00 << 7)
#define ZMK_HID_MAIN_VAL_VOL (0x01 << 7)

#define HID_USAGE_PAGE_2(a, b) HID_ITEM(HID_ITEM_TAG_USAGE_PAGE, HID_ITEM_TYPE_GLOBAL, 2), a, b

#define RAW_USAGE_PAGE 0xFF60
#define RAW_USAGE_ID 0x61
#define RAW_EPSIZE 32

static const uint8_t zmk_hid_launcher_report_desc[] = {
    HID_USAGE_PAGE_2((RAW_USAGE_PAGE & 0xff), (RAW_USAGE_PAGE >> 8)), // Vendor Defined
    HID_USAGE(RAW_USAGE_ID),                                          // Vendor Defined
    HID_COLLECTION(0x01),                                             // Application
                                                                      // Data to host
    HID_USAGE(0x62),                                                  // Vendor Defined
    HID_LOGICAL_MIN8(0x00), HID_LOGICAL_MAX16(0xFF, 0x00), HID_REPORT_COUNT(RAW_EPSIZE),
    HID_REPORT_SIZE(0x08),
    HID_INPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS),

    // Data from host
    HID_USAGE(0x63), // Vendor Defined
    HID_LOGICAL_MIN8(0x00), HID_LOGICAL_MAX16(0xFF, 0x00), HID_REPORT_COUNT(RAW_EPSIZE),
    HID_REPORT_SIZE(0x08),
    HID_OUTPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS |
               ZMK_HID_MAIN_VAL_NON_VOL),

    HID_END_COLLECTION};

enum launcher_command_id {
    id_get_protocol_version = 0x01, // always 0x01
    id_get_keyboard_value = 0x02,
    id_set_keyboard_value = 0x03,
    id_dynamic_keymap_get_keycode = 0x04,
    id_dynamic_keymap_set_keycode = 0x05,
    id_dynamic_keymap_reset = 0x06,
    id_custom_set_value = 0x07,
    id_custom_get_value = 0x08,
    id_custom_save = 0x09,
    id_eeprom_reset = 0x0A,
    id_bootloader_jump = 0x0B,
    id_dynamic_keymap_macro_get_count = 0x0C,
    id_dynamic_keymap_macro_get_buffer_size = 0x0D,
    id_dynamic_keymap_macro_get_buffer = 0x0E,
    id_dynamic_keymap_macro_set_buffer = 0x0F,
    id_dynamic_keymap_macro_reset = 0x10,
    id_dynamic_keymap_get_layer_count = 0x11,
    id_dynamic_keymap_get_buffer = 0x12,
    id_dynamic_keymap_set_buffer = 0x13,
    id_dynamic_keymap_get_encoder = 0x14,
    id_dynamic_keymap_set_encoder = 0x15,

    id_openrgb = 0x16, // OpenRGB direct RGB control (ZMK issue #893)

    kc_get_protocol_version = 0xa0,
    kc_get_firmware_version = 0xa1, // ascii ver
    kc_get_support_feature = 0xa2,
    kc_get_default_layer = 0xa3,
    kc_get_set_debounce = 0xa7,
    kc_set_key_debounce = 0x80,
    kc_get_key_debounce = 0x00,

    id_unhandled = 0xFF,
};

// OpenRGB direct-control command handler (src/launcher/openrgb.c).
void openrgb_handle_command(uint8_t *data, uint8_t length);

enum {
    FEATURE_DEFAULT_LAYER = 0x01 << 0,
    FEATURE_BLUETOOTH = 0x01 << 1,
    FEATURE_P24G = 0x01 << 2,
    FEATURE_ANALOG_MATRIX = 0x01 << 3,
    FEATURE_INFO_CHAGNED_NOTIFY = 0x01 << 4,
    FEATURE_DYNAMIC_DEBOUNCE = 0x01 << 5,
    FEATURE_SNAP_CLICK = 0x01 << 6,
    FEATURE_KEYCHRON_RGB = 0x01 << 7,
};
enum launcher_keyboard_value_id {
    id_uptime = 0x01,
    id_layout_options = 0x02,
    id_switch_matrix_state = 0x03,
    id_firmware_version = 0x04,
    id_device_indication = 0x05,
};

enum launcher_channel_id {
    id_custom_channel = 0,
    id_zmk_backlight_channel = 1,
    id_zmk_rgblight_channel = 2,
    id_zmk_rgb_matrix_channel = 3,
    id_zmk_audio_channel = 4,
    id_zmk_led_matrix_channel = 5,
};

enum launcher_zmk_backlight_value {
    id_zmk_backlight_brightness = 1,
    id_zmk_backlight_effect = 2,
};

enum launcher_zmk_rgblight_value {
    id_zmk_rgblight_brightness = 1,
    id_zmk_rgblight_effect = 2,
    id_zmk_rgblight_effect_speed = 3,
    id_zmk_rgblight_color = 4,
};

enum launcher_zmk_rgb_matrix_value {
    id_zmk_rgb_matrix_brightness = 1,
    id_zmk_rgb_matrix_effect = 2,
    id_zmk_rgb_matrix_effect_speed = 3,
    id_zmk_rgb_matrix_color = 4,
};

enum launcher_zmk_led_matrix_value {
    id_zmk_led_matrix_brightness = 1,
    id_zmk_led_matrix_effect = 2,
    id_zmk_led_matrix_effect_speed = 3,
};

enum launcher_zmk_audio_value {
    id_zmk_audio_enable = 1,
    id_zmk_audio_clicky_enable = 2,
};

typedef uint32_t matrix_row_t;

enum {
    BT_HST1 = QK_KB_0,
    BT_HST2,
    BT_HST3,
    KC_BOOT,
    PAIR_24G,
    MAC_SCRN_LOCK,           // Screen lock in macOS
    UC_CMD_CMA,              // Command-Comma (,): Open preferences for the front app.
    UC_LOPT,                 // Left Option
    UC_ROPT,                 // Right Option
    UC_LCMD,                 // Left Command
    UC_RCMD,                 // Right Command
    UC_CTRL_LEFT,            // ctrl+ <-  ,switch desktops
    UC_CTRL_RIGHT,           // ctrl+ ->  ,switch desktops
    UC_EMOJI_MAC,            // Command+Control+space open emoji in mac
    UC_TASK_VIEW,            // LG(TAB) Open Task View
    UC_SWITCH_DESKTOP_LEFT,  // LG(LC(LEFT)) switch desktops
    UC_SWITCH_DESKTOP_RIGHT, // LG(LC(RIGHT)) switch desktops
    UC_FILE_EXPLORER,        // LG(E) ,Open File Explorer.
    UC_LOCK,                 // LG(L),Lock your PC or switch accounts.
    UC_SETTINGS,             // LG(I),Open Settings.
    UC_EMOJI_WIN,            // LG(DOT),open emoji in win
    UC_PRNS_WIN,             // LG(LS(S))
    UC_PRNS_MAC,             // LG(LS(N4))
    UC_BATINFO,
    UC_SIRI,    // LG(SPACE)
    UC_CORTANA, // LG(C)
#if CONFIG_SOFTWARE_SWITCH_LAYER
    UC_MAC_LAYER,
    UC_WIN_LAYER,
#endif
#if CONFIG_MAC_VIA_FUNC
    UC_MAC_MCTL, // Misson Control in Mac
    UC_MAC_LPAD, // Lanuch Pad in Mac
#endif
};

#define ZMK_BUILDDATE "2025-05-24"
#define MATRIX_COLS ZMK_MATRIX_COLS
#define MATRIX_ROWS (ZMK_MATRIX_ROWS - 1)
#define KEYMAP_LEN (MATRIX_COLS * MATRIX_ROWS * 2)
#if CONFIG_LED_STRIP
#define RGB_MATRIX_ENABLE
#endif
#if ZMK_KEYMAP_SENSORS_LEN
#define ENCODER_MAP_ENABLE
#endif
#define EECONFIG_SIZE 0

#define TOTAL_EEPROM_BYTE_COUNT 2560

#define LAUNCHER_EEPROM_MAGIC_ADDR (EECONFIG_SIZE)

#define LAUNCHER_EEPROM_LAYOUT_OPTIONS_ADDR (LAUNCHER_EEPROM_MAGIC_ADDR + 3)

#define LAUNCHER_EEPROM_LAYOUT_OPTIONS_SIZE 1

#define LAUNCHER_EEPROM_LAYOUT_OPTIONS_DEFAULT 0x00000000

#define LAUNCHER_EEPROM_CUSTOM_CONFIG_ADDR                                                         \
    (LAUNCHER_EEPROM_LAYOUT_OPTIONS_ADDR + LAUNCHER_EEPROM_LAYOUT_OPTIONS_SIZE)

#define LAUNCHER_EEPROM_CUSTOM_CONFIG_SIZE 0

#define LAUNCHER_EEPROM_CONFIG_END                                                                 \
    (LAUNCHER_EEPROM_CUSTOM_CONFIG_ADDR + LAUNCHER_EEPROM_CUSTOM_CONFIG_SIZE)

#define DYNAMIC_KEYMAP_EEPROM_START (LAUNCHER_EEPROM_CONFIG_END)

#define NUM_ENCODERS ZMK_KEYMAP_SENSORS_LEN

#define DYNAMIC_KEYMAP_LAYER_COUNT ZMK_KEYMAP_LAYERS_LEN

#define DYNAMIC_KEYMAP_MACRO_COUNT 16

#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR (TOTAL_EEPROM_BYTE_COUNT - 1)

#define DYNAMIC_KEYMAP_EEPROM_ADDR DYNAMIC_KEYMAP_EEPROM_START

#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR                                                         \
    (DYNAMIC_KEYMAP_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2))

#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR                                                           \
    (DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR + (DYNAMIC_KEYMAP_LAYER_COUNT * NUM_ENCODERS * 2 * 2))

#define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE                                                           \
    (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR - DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR + 1)

#define NUM_KEYMAP_LAYERS_RAW DYNAMIC_KEYMAP_LAYER_COUNT

#define LAUNCHER_PROTOCOL_VERSION 0x000C

#define VER_STR1(a1, a2, a3, a4) "v" #a1 "." #a2 "." #a3 "-" a4
#define VER_STR(a1, a2, a3, a4) VER_STR1(a1, a2, a3, a4)
#define EXTRAVERSION "ZK"
#define LAUNCHER_VERSION_STRING                                                                    \
    VER_STR(APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL, EXTRAVERSION)
#define LAUNCHER_FIRMWARE_VERSION                                                                  \
    (APP_VERSION_MAJOR << 16 | APP_VERSION_MINOR << 8 | APP_PATCHLEVEL) // 0x00000000
extern uint32_t tap_code_delay_us;
#define TAP_CODE_DELAY tap_code_delay_us // 0
#define DYNAMIC_KEYMAP_MACRO_DELAY TAP_CODE_DELAY

enum {
    LAUNCHER_PATH_USB,
    LAUNCHER_PATH_BLE,
    LAUNCHER_PATH_PPT,
};
bool launcher_eeprom_is_valid(void);

void launcher_eeprom_set_valid(bool valid);

void eeconfig_init_launcher(void);
int launcher_init(void);

uint32_t launcher_get_layout_options(void);
void launcher_set_layout_options(uint32_t value);
void launcher_set_layout_options_kb(uint32_t value);

void launcher_set_device_indication(uint8_t value);

#if defined(BACKLIGHT_ENABLE)
void launcher_zmk_backlight_command(uint8_t *data, uint8_t length);
void launcher_zmk_backlight_set_value(uint8_t *data);
void launcher_zmk_backlight_get_value(uint8_t *data);
void launcher_zmk_backlight_save(void);
#endif

#if defined(RGBLIGHT_ENABLE)
void launcher_zmk_rgblight_command(uint8_t *data, uint8_t length);
void launcher_zmk_rgblight_set_value(uint8_t *data);
void launcher_zmk_rgblight_get_value(uint8_t *data);
void launcher_zmk_rgblight_save(void);
#endif

#if defined(RGB_MATRIX_ENABLE)
void launcher_zmk_rgb_matrix_command(uint8_t *data, uint8_t length);
void launcher_zmk_rgb_matrix_set_value(uint8_t *data);
void launcher_zmk_rgb_matrix_get_value(uint8_t *data);
void launcher_zmk_rgb_matrix_save(void);
#endif

#if defined(LED_MATRIX_ENABLE)
void launcher_zmk_led_matrix_command(uint8_t *data, uint8_t length);
void launcher_zmk_led_matrix_set_value(uint8_t *data);
void launcher_zmk_led_matrix_get_value(uint8_t *data);
void launcher_zmk_led_matrix_save(void);
#endif

#if defined(AUDIO_ENABLE)
void launcher_zmk_audio_command(uint8_t *data, uint8_t length);
void launcher_zmk_audio_set_value(uint8_t *data);
void launcher_zmk_audio_get_value(uint8_t *data);
void launcher_zmk_audio_save(void);
#endif

void raw_hid_receive(uint8_t *data, uint8_t length);

union LAUNCHER_DEVICE {
    uint8_t raw[TOTAL_EEPROM_BYTE_COUNT];
    struct {
        uint8_t magic[3];
        uint8_t layout_options[LAUNCHER_EEPROM_LAYOUT_OPTIONS_SIZE];
#if LAUNCHER_EEPROM_CUSTOM_CONFIG_SIZE
        uint8_t custom[LAUNCHER_EEPROM_CUSTOM_CONFIG_SIZE];
#endif
        uint8_t keymaps[(DYNAMIC_KEYMAP_LAYER_COUNT * KEYMAP_LEN)];
#if NUM_ENCODERS
        uint8_t encoder[DYNAMIC_KEYMAP_LAYER_COUNT * NUM_ENCODERS * 2 * 2];
#endif
        uint8_t macros[DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE];
    } __packed;
};

typedef union LAUNCHER_DEVICE _launcher_device;
extern _launcher_device launcher_device;

#if CONFIG_LAUNCHER_USER_SET_DEBOUNCE
struct user_debounce {
    uint8_t scan_period_ms;
    uint8_t debounce_press_ms;
    uint8_t debounce_release_ms;
    uint8_t div : 7; // add for report rate;
    uint8_t mac_win_layer : 1;
#if CONFIG_ADD_PPT_REPORT_RATE
    uint8_t ppt_div; // add for ppt report rate;
#endif
};
#endif
void launcher_read_all(void);
void launcher_read_macros(void);
void launcher_read_keymaps(void);
void launcher_read_magic(void);
void launcher_read_debounce(void);
void launcher_update_debounce(void);
void launcher_update_magic(void);
void launcher_update_layout(void);
void launcher_update_custom(void);
void launcher_update_keymaps(void);
void launcher_update_keymap(uint8_t i);
void launcher_update_encoder(void);
void launcher_update_macros(void);
void launcher_delete_macros(void);
void launcher_read_changed_keys(void);
void launcher_update_changed_keys(uint8_t layer);
void launcher_set_changed_key(uint8_t layer, uint8_t row, uint8_t col);
void launcher_reset_changed_keys(void);
void launcher_reset_keymaps(void);
void set_zmk_keymap(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);
void set_zmk_encoders(uint8_t layer, bool clockwise, uint16_t keycode);
void generate_launcher_keymaps(void);
void launcher_save_debounce(void);
void launcher_set_path(uint8_t path);

static inline uint8_t eeprom_read_byte(uint16_t addr) { return launcher_device.raw[addr]; }
static inline void eeprom_update_byte(uint16_t addr, uint8_t value) {
    launcher_device.raw[addr] = value;
}