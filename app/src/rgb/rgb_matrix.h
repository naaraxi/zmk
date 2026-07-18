/* Copyright 2017 Jason Williams
 * Copyright 2017 Jack Humbert
 * Copyright 2018 Yiancar
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "rgb_matrix_types.h"
#include <zephyr/kernel.h>
#include "./lib8tion/lib8tion.h"

typedef bool (*rgb_effect_func)(effect_params_t *params);

struct rgb_matrix_limits_t {
    uint8_t led_min_index;
    uint8_t led_max_index;
};

struct rgb_matrix_limits_t zmk_rgb_matrix_get_limits(uint8_t iter);

#define RGB_MATRIX_USE_LIMITS_ITER(min, max, iter)                                                 \
    struct rgb_matrix_limits_t limits = zmk_rgb_matrix_get_limits(iter);                           \
    uint8_t min = limits.led_min_index;                                                            \
    uint8_t max = limits.led_max_index;                                                            \
    (void)min;                                                                                     \
    (void)max;

#define RGB_MATRIX_USE_LIMITS(min, max) RGB_MATRIX_USE_LIMITS_ITER(min, max, params->iter)

#define RGB_MATRIX_INDICATOR_SET_COLOR(i, r, g, b)                                                 \
    if (i >= led_min && i < led_max) {                                                             \
        zmk_rgb_matrix_set_color(i, r, g, b);                                                      \
    }

#define RGB_MATRIX_TEST_LED_FLAGS()                                                                \
    if (!HAS_ANY_FLAGS(g_led_config.flags[i], params->flags))                                      \
    continue

uint8_t zmk_rgb_matrix_map_row_column_to_led_kb(uint8_t row, uint8_t column, uint8_t *led_i);
uint8_t zmk_rgb_matrix_map_row_column_to_led(uint8_t row, uint8_t column, uint8_t *led_i);

void zmk_rgb_matrix_set_color(int index, uint8_t red, uint8_t green, uint8_t blue);
void zmk_rgb_matrix_set_color_all(uint8_t red, uint8_t green, uint8_t blue);
void zmk_rgb_matrix_region_set_color(uint8_t region, int index, uint8_t red, uint8_t green,
                                     uint8_t blue);
void zmk_rgb_matrix_region_set_color_all(uint8_t region, uint8_t red, uint8_t green, uint8_t blue);

void process_rgb_matrix(uint8_t row, uint8_t col, bool pressed);

void zmk_rgb_matrix_task(void);

// This runs after another backlight effect and replaces
// colors already set
void zmk_rgb_matrix_indicators(void);
bool zmk_rgb_matrix_indicators_kb(void);
bool zmk_rgb_matrix_indicators_user(void);

void zmk_rgb_matrix_indicators_advanced(effect_params_t *params);
bool zmk_rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max);
bool zmk_rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max);

// void zmk_rgb_matrix_init(const struct device *_arg);

void zmk_rgb_matrix_reload_from_eeprom(void);

// --- OpenRGB direct control (ZMK issue #893) ---
// The host (OpenRGB) takes over the LED buffer over the raw-HID channel; the
// effect renderer is suppressed so host-set colors persist, and control is
// auto-handed-back to the saved onboard effect after a short idle (host quit /
// USB unplug / switch to BLE or 2.4GHz).
void zmk_rgb_matrix_openrgb_enter(void);
void zmk_rgb_matrix_openrgb_exit(void);
void zmk_rgb_matrix_openrgb_feed(void);
bool zmk_rgb_matrix_openrgb_active(void);

// True while `led` is being driven as an active OS lock indicator (caps/num/
// etc.). The OpenRGB direct path skips these so the lock overlay owns them and
// the host's writes can't race it (defined in keychron_rgb.c).
bool os_indicator_owns_led(uint16_t led);

void zmk_rgb_matrix_set_suspend_state(bool state);
bool zmk_rgb_matrix_get_suspend_state(void);
void zmk_rgb_matrix_toggle(void);
void zmk_rgb_matrix_toggle_no_save(void);
void zmk_rgb_matrix_enable(void);
void zmk_rgb_matrix_disable(void);
uint8_t zmk_rgb_matrix_is_enabled(void);
void zmk_rgb_matrix_mode(uint8_t mode);
void zmk_rgb_matrix_mode_no_save(uint8_t mode);
uint8_t zmk_rgb_matrix_get_mode(void);
void zmk_rgb_matrix_step(void);
void zmk_rgb_matrix_step_reverse(void);
void zmk_rgb_matrix_sethsv(uint16_t hue, uint8_t sat, uint8_t val);
void zmk_rgb_matrix_sethsv_no_save(uint16_t hue, uint8_t sat, uint8_t val);
HSV zmk_rgb_matrix_get_hsv(void);
uint8_t zmk_rgb_matrix_get_hue(void);
uint8_t zmk_rgb_matrix_get_sat(void);
uint8_t zmk_rgb_matrix_get_val(void);
void zmk_rgb_matrix_increase_hue(void);
void zmk_rgb_matrix_decrease_hue(void);
void zmk_rgb_matrix_increase_sat(void);
void zmk_rgb_matrix_decrease_sat(void);
void zmk_rgb_matrix_increase_val(void);
void zmk_rgb_matrix_decrease_val(void);
void zmk_rgb_matrix_set_speed_no_save(uint8_t speed);
void zmk_rgb_matrix_set_speed(uint8_t speed);
uint8_t zmk_rgb_matrix_get_speed(void);
void zmk_rgb_matrix_increase_speed(void);
void zmk_rgb_matrix_decrease_speed(void);
led_flags_t zmk_rgb_matrix_get_flags(void);
void zmk_rgb_matrix_set_flags(led_flags_t flags);

RGB rgb_matrix_hsv_to_rgb(HSV hsv);
void zmk_rgb_matrix_update_pwm_buffers(void);
void zmk_rgb_matrix_driver_init(void);
int zmk_rgb_matrix_on(void);
int zmk_rgb_matrix_off(void);
int zmk_rgb_led_indicatots_on();
int zmk_rgb_led_indicatots_off();
uint8_t zmk_rgb_get_onoff_status(void);
void save_rgb_matrix_config();
void zmk_rgb_sleep(void);
bool rgb_is_allow_sleep(void);
void enable_rgb_thread(void);

static inline bool zmk_rgb_matrix_check_finished_leds(uint8_t led_idx) {

    return led_idx < RGB_MATRIX_LED_COUNT;
}
static inline uint32_t k_uptime_delta_32(uint32_t reftime) {
    uint32_t uptime = k_uptime_get_32();
    if (uptime >= reftime) {
        return uptime - reftime;
    } else {
        return (UINT32_MAX + 1 - reftime + uptime);
    }
}
typedef struct {
    uint8_t leds[10];
    uint8_t led_count;
    RGB rgb[10];
    uint16_t led_on_time;
    uint16_t led_off_time;
    int16_t led_flash_count;
    // uint8_t rgb_mode;
    uint8_t bat_level;
    uint32_t bat_low_start_time;
    uint8_t rgb_enable : 1;
    uint8_t init : 1;
    uint8_t running : 1;
    uint8_t exclude : 1;
    uint8_t bat_info : 1;
    uint8_t bat_low : 1;
} _led_indicators;
extern _led_indicators rgb_led_indicators;

extern rgb_config_t rgb_matrix_config;

extern uint32_t g_rgb_timer;
extern led_config_t g_led_config;

extern last_hit_t g_last_hit_tracker;

extern uint8_t g_rgb_frame_buffer[MATRIX_ROWS][MATRIX_COLS];

extern rgb_effect_func rgb_effect_funcs[];
extern const led_point_t k_rgb_matrix_center;

extern bool rgb_control_enable;
uint8_t get_rgb_test_start(void);
void save_rgb_matrix_config_rightnow(void);