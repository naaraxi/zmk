/* Copyright 2024 @ Keychron (https://www.keychron.com)
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

#include "keychron_rgb_type.h"
#include "../rgb_matrix_conf.h"
#include "rgb_matrix_kb_config.h"
#include <zephyr/settings/settings.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zmk/leds.h>
#include "snap_click.h"
#include "retail_demo.h"

LOG_MODULE_REGISTER(kc_rgb, 0);

static int keychron_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                               void *cb_arg);
struct settings_handler keychron_set = {.name = "keychron", .h_set = keychron_handle_set};
extern RGB hsv_to_rgb(HSV hsv);

#define PER_KEY_RGB_VER 0x0001
enum {
    RGB_GET_PROTOCOL_VER = 0x01,
    RGB_SAVE,
    GET_INDICATORS_CONFIG,
    SET_INDICATORS_CONFIG,
    RGB_GET_LED_COUNT,
    RGB_GET_LED_IDX,
    PER_KEY_RGB_GET_TYPE,
    PER_KEY_RGB_SET_TYPE,
    PER_KEY_RGB_GET_COLOR,
    PER_KEY_RGB_SET_COLOR, // 10
    MIXED_EFFECT_RGB_GET_INFO,
    MIXED_EFFECT_RGB_GET_REGIONS,
    MIXED_EFFECT_RGB_SET_REGIONS,
    MIXED_EFFECT_RGB_GET_EFFECT_LIST,
    MIXED_EFFECT_RGB_SET_EFFECT_LIST,
};

extern uint8_t per_key_rgb_type;
extern HSV per_key_led[RGB_MATRIX_LED_COUNT];
extern HSV default_per_key_led[RGB_MATRIX_LED_COUNT];

extern uint8_t regions[RGB_MATRIX_LED_COUNT];
extern uint8_t rgb_regions[RGB_MATRIX_LED_COUNT];
extern effect_config_t effect_list[EFFECT_LAYERS][EFFECTS_PER_LAYER];
extern uint8_t default_region[RGB_MATRIX_LED_COUNT];

os_indicator_config_t os_ind_cfg;
uint8_t retail_demo_enable;

extern void update_mixed_rgb_effect_count(void);

void eeconfig_reset_custom_rgb(void) {
    os_ind_cfg.disable.raw = 0;
    os_ind_cfg.hsv.s = 0;
    os_ind_cfg.hsv.h = os_ind_cfg.hsv.v = 0xFF;

    retail_demo_enable = 0;
    per_key_rgb_type = 0;
    memcpy(per_key_led, default_per_key_led, sizeof(per_key_led));
    memcpy(regions, default_region, RGB_MATRIX_LED_COUNT);
    memset(effect_list, 0, sizeof(effect_list));

    effect_list[0][0].effect = 5;
    effect_list[0][0].sat = 255;
    effect_list[0][0].speed = 127;
    effect_list[0][0].time = 5000;

    effect_list[1][0].effect = 2;
    effect_list[1][0].hue = 0;
    effect_list[1][0].sat = 255;
    effect_list[1][0].speed = 127;
    effect_list[1][0].time = 5000;

    // update_mixed_rgb_effect_count();
    // kc_rgb_save();
}

int eeconfig_init_custom_rgb(void) {
    eeconfig_reset_custom_rgb();

    int rc = settings_subsys_init();
    if (rc) {
        printk("settings subsys initialization: fail (err %d)\n", rc);
        return rc;
    }
    rc = settings_register(&keychron_set);
    if (rc) {
        LOG_ERR("Failed to setup the profile settings handler (err %d)", rc);
        return rc;
    }
    settings_load_subtree("keychron");

    update_mixed_rgb_effect_count();
#ifdef CONFIG_RETAIL_DEMO_ENABLE
    if (retail_demo_enable && zmk_rgb_matrix_get_mode() != RGB_EFFECT_MIXED_RGB) {
        retail_demo_start();
    }
#endif
    return 0;
}

void rgb_save_retail_demo(void) {
    settings_save_one("keychron/retail_demo_enable", (const void *)&retail_demo_enable,
                      sizeof(retail_demo_enable));
}

static bool rgb_get_version(uint8_t *data) {
    data[1] = PER_KEY_RGB_VER & 0xFF;
    data[2] = (PER_KEY_RGB_VER >> 8) & 0xFF;

    return true;
}

static bool rgb_get_led_count(uint8_t *data) {
    data[1] = RGB_MATRIX_LED_COUNT;

    return true;
}

static bool rgb_get_led_idx(uint8_t *data) {
    uint8_t row = data[0];
    if (row > MATRIX_ROWS)
        return false;

    uint8_t led_idx[128];
    uint32_t row_mask = 0;
    memcpy(&row_mask, &data[1], 3);

    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        led_idx[0] = 0xFF;
        if (row_mask & (0x01 << c)) {
            zmk_rgb_matrix_map_row_column_to_led(row, c, led_idx);
        }
        data[1 + c] = led_idx[0];
    }

    return true;
}

static bool per_key_rgb_get_type(uint8_t *data) {
    extern uint8_t per_key_rgb_type;
    data[1] = per_key_rgb_type;

    return true;
}

static bool per_key_rgb_set_type(uint8_t *data) {
    uint8_t type = data[0];

    if (type >= PER_KEY_RGB_MAX)
        return false;

    per_key_rgb_type = data[0];

    return true;
}

static bool per_key_rgb_get_led_color(uint8_t *data) {
    uint8_t start = data[0];
    uint8_t count = data[1];

    if (count > 9)
        return false;

    for (uint8_t i = 0; i < count; i++) {
        data[1 + i * 3] = per_key_led[start + i].h;
        data[2 + i * 3] = per_key_led[start + i].s;
        data[3 + i * 3] = per_key_led[start + i].v;
    }

    return true;
}

static bool per_key_rgb_set_led_color(uint8_t *data) {
    uint8_t start = data[0];
    uint8_t count = data[1];

    if (count > 9 || start + count > RGB_MATRIX_LED_COUNT)
        return false;

    for (uint8_t i = 0; i < count; i++) {
        per_key_led[start + i].h = data[2 + i * 3];
        per_key_led[start + i].s = data[3 + i * 3];
        per_key_led[start + i].v = data[4 + i * 3];
    }

    return true;
}

static bool mixed_rgb_get_effect_info(uint8_t *data) {
    data[1] = EFFECT_LAYERS;
    data[2] = EFFECTS_PER_LAYER;

    return true;
}

static bool mixed_rgb_get_regions(uint8_t *data) {
    uint8_t start = data[0];
    uint8_t count = data[1];

    if (count > 29 || start + count > RGB_MATRIX_LED_COUNT)
        return false;
    memcpy(&data[1], &regions[start], count);

    return true;
}

bool mixed_rgb_set_regions(uint8_t *data) {
    uint8_t start = data[0];
    uint8_t count = data[1];

    if (count > 28 || start + count > RGB_MATRIX_LED_COUNT)
        return false;
    for (uint8_t i = 0; i < count; i++)
        if (data[2 + i] >= EFFECT_LAYERS)
            return false;

    memcpy(&regions[start], &data[2], count);
    memcpy(&rgb_regions[start], &data[2], count);

    return true;
}
#define EFFECT_DATA_LEN 8

static bool mixed_rgb_get_effect_list(uint8_t *data) {
    uint8_t region = data[0];
    uint8_t start = data[1];
    uint8_t count = data[2];

    if (count > 3 || region > EFFECT_LAYERS || start + count > EFFECTS_PER_LAYER)
        return false;

    for (uint8_t i = 0; i < count; i++) {
        data[1 + i * EFFECT_DATA_LEN] = effect_list[region][start + i].effect;
        data[2 + i * EFFECT_DATA_LEN] = effect_list[region][start + i].hue;
        data[3 + i * EFFECT_DATA_LEN] = effect_list[region][start + i].sat;
        data[4 + i * EFFECT_DATA_LEN] = effect_list[region][start + i].speed;
        memcpy(&data[5 + i * EFFECT_DATA_LEN], &effect_list[region][start + i].time, 4);
    }

    return true;
}

bool mixed_rgb_set_effect_list(uint8_t *data) {
    uint8_t region = data[0];
    uint8_t start = data[1];
    uint8_t count = data[2];

    if (count > 3 || region > EFFECT_LAYERS || start + count > EFFECTS_PER_LAYER)
        return false;
    for (uint8_t i = 0; i < count; i++) {
        if (data[3 + i * EFFECT_DATA_LEN] >= RGB_EFFECT_MIXED_RGB)
            return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        effect_list[region][start + i].effect = data[3 + i * EFFECT_DATA_LEN];
        effect_list[region][start + i].hue = data[4 + i * EFFECT_DATA_LEN];
        effect_list[region][start + i].sat = data[5 + i * EFFECT_DATA_LEN];
        effect_list[region][start + i].speed = data[6 + i * EFFECT_DATA_LEN];
        memcpy(&effect_list[region][start + i].time, &data[7 + i * EFFECT_DATA_LEN], 4);
    }
    update_mixed_rgb_effect_count();

    return true;
}
void kc_rgb_save_worker(struct k_work *work) {
    settings_save_one("keychron/retail_demo_enable", (const void *)&retail_demo_enable,
                      sizeof(retail_demo_enable));
    settings_save_one("keychron/os_ind_cfg", (const void *)&os_ind_cfg, sizeof(os_ind_cfg));
    settings_save_one("keychron/per_key_rgb_type", (const void *)&per_key_rgb_type,
                      sizeof(per_key_rgb_type));
    settings_save_one("keychron/per_key_led", (const void *)per_key_led, sizeof(per_key_led));
    settings_save_one("keychron/regions", (const void *)regions, sizeof(regions));
    settings_save_one("keychron/effect_list", (const void *)effect_list, sizeof(effect_list));
}
K_WORK_DELAYABLE_DEFINE(kc_rgb_save_work, kc_rgb_save_worker);

static bool kc_rgb_save(void) {
    k_work_reschedule(&kc_rgb_save_work, K_MSEC(300));
    return true;
}

static bool get_indicators_config(uint8_t *data) {
    data[1] = 0
#if defined(NUM_LOCK_INDEX) && !defined(DIM_NUM_LOCK)
              | (1 << 0x00)
#endif
#if defined(CAPS_LOCK_INDEX) && !defined(DIM_CAPS_LOCK)
              | (1 << 0x01)
#endif
#if defined(SCROLL_LOCK_INDEX)
              | (1 << 0x02)
#endif
#if defined(COMPOSE_LOCK_INDEX)
              | (1 << 0x03)
#endif
#if defined(KANA_LOCK_INDEX)
              | (1 << 0x04)
#endif
        ;
    data[2] = os_ind_cfg.disable.raw;
    data[3] = os_ind_cfg.hsv.h;
    data[4] = os_ind_cfg.hsv.s;
    data[5] = os_ind_cfg.hsv.v;

    return true;
}

static bool set_indicators_config(uint8_t *data) {
    os_ind_cfg.disable.raw = data[0];
    os_ind_cfg.hsv.h = data[1];
    os_ind_cfg.hsv.s = data[2];
    os_ind_cfg.hsv.v = data[3];

    if (os_ind_cfg.hsv.v < 128)
        os_ind_cfg.hsv.v = 128;
    keyboad_led_set_onoff(keyboard_get_led_state().raw);
    // LOG_ERR("set indicate,disable:%x,h:%x,s:%x,v:%x",data[0],data[1],data[2],data[3]);

    return true;
}

void kc_rgb_matrix_rx(uint8_t *data, uint8_t length) {
    uint8_t cmd = data[1];
    bool success = true;

    switch (cmd) {
    case RGB_GET_PROTOCOL_VER:
        success = rgb_get_version(&data[2]);
        break;

    case RGB_SAVE:
        success = kc_rgb_save();
        break;

    case GET_INDICATORS_CONFIG:
        success = get_indicators_config(&data[2]);
        break;

    case SET_INDICATORS_CONFIG:
        success = set_indicators_config(&data[2]);
        break;

    case RGB_GET_LED_COUNT:
        success = rgb_get_led_count(&data[2]);
        break;

    case RGB_GET_LED_IDX:
        success = rgb_get_led_idx(&data[2]);
        break;

    case PER_KEY_RGB_GET_TYPE:
        success = per_key_rgb_get_type(&data[2]);
        break;

    case PER_KEY_RGB_SET_TYPE:
        success = per_key_rgb_set_type(&data[2]);
        break;

    case PER_KEY_RGB_GET_COLOR:
        success = per_key_rgb_get_led_color(&data[2]);
        break;

    case PER_KEY_RGB_SET_COLOR:
        success = per_key_rgb_set_led_color(&data[2]);
        break;

    case MIXED_EFFECT_RGB_GET_INFO:
        success = mixed_rgb_get_effect_info(&data[2]);
        break;

    case MIXED_EFFECT_RGB_GET_REGIONS:
        success = mixed_rgb_get_regions(&data[2]);
        break;

    case MIXED_EFFECT_RGB_SET_REGIONS:
        success = mixed_rgb_set_regions(&data[2]);
        break;

    case MIXED_EFFECT_RGB_GET_EFFECT_LIST:
        success = mixed_rgb_get_effect_list(&data[2]);
        break;

    case MIXED_EFFECT_RGB_SET_EFFECT_LIST:
        success = mixed_rgb_set_effect_list(&data[2]);
        break;

    default:
        data[0] = 0xFF;
        break;
    }

    data[2] = success ? 0 : 1;
}

void os_state_indicate(void) {

    RGB rgb = hsv_to_rgb(os_ind_cfg.hsv);

#if defined(NUM_LOCK_INDEX)
    if (keyboard_get_led_state().num_lock && !os_ind_cfg.disable.num_lock) {
        zmk_rgb_matrix_set_color(NUM_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(CAPS_LOCK_INDEX)
    if (keyboard_get_led_state().caps_lock && !os_ind_cfg.disable.caps_lock) {
        zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(SCROLL_LOCK_INDEX)
    if (keyboard_get_led_state().scroll_lock && !os_ind_cfg.disable.scroll_lock) {
        zmk_rgb_matrix_set_color(SCROLL_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(COMPOSE_LOCK_INDEX)
    if (keyboard_get_led_state().compose && !os_ind_cfg.disable.compose) {
        zmk_rgb_matrix_set_color(COMPOSE_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(KANA_LOCK_INDEX)
    if (keyboard_get_led_state().kana && !os_ind_cfg.disable.kana) {
        zmk_rgb_matrix_set_color(KANA_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
    (void)rgb;
}

/*
 * True when `led` is currently being driven as an OS lock indicator (caps/num/
 * etc. active and not user-disabled). The OpenRGB direct path uses this to skip
 * those keys, so os_state_indicate() is the sole writer for them and the host's
 * per-frame writes can't race the overlay (which caused visible flicker).
 * Conditions must stay in lockstep with os_state_indicate() above.
 */
bool os_indicator_owns_led(uint16_t led) {
#if defined(NUM_LOCK_INDEX)
    if (led == NUM_LOCK_INDEX && keyboard_get_led_state().num_lock && !os_ind_cfg.disable.num_lock) {
        return true;
    }
#endif
#if defined(CAPS_LOCK_INDEX)
    if (led == CAPS_LOCK_INDEX && keyboard_get_led_state().caps_lock &&
        !os_ind_cfg.disable.caps_lock) {
        return true;
    }
#endif
#if defined(SCROLL_LOCK_INDEX)
    if (led == SCROLL_LOCK_INDEX && keyboard_get_led_state().scroll_lock &&
        !os_ind_cfg.disable.scroll_lock) {
        return true;
    }
#endif
#if defined(COMPOSE_LOCK_INDEX)
    if (led == COMPOSE_LOCK_INDEX && keyboard_get_led_state().compose &&
        !os_ind_cfg.disable.compose) {
        return true;
    }
#endif
#if defined(KANA_LOCK_INDEX)
    if (led == KANA_LOCK_INDEX && keyboard_get_led_state().kana && !os_ind_cfg.disable.kana) {
        return true;
    }
#endif
    return false;
}

extern snap_click_config_t snap_click_pair[SNAP_CLICK_COUNT];

static int keychron_handle_set(const char *name, size_t len, settings_read_cb read_cb,
                               void *cb_arg) {

    LOG_DBG("Setting keychron rgb value %s", name);

    if (settings_name_steq(name, "os_ind_cfg", NULL)) {
        if (len != sizeof(os_ind_cfg)) {
            LOG_ERR("Invalid os_ind_cfg size (got %d expected %d)", len, sizeof(os_ind_cfg));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &os_ind_cfg, sizeof(os_ind_cfg));
        if (err <= 0) {
            LOG_ERR("Failed to read os_ind_cfg  from settings (err %d)", err);
            return err;
        }
        if (os_ind_cfg.hsv.v < 128)
            os_ind_cfg.hsv.v = 128;
    } else if (settings_name_steq(name, "per_key_rgb_type", NULL)) {
        if (len != sizeof(per_key_rgb_type)) {
            LOG_ERR("Invalid per_key_rgb_type size (got %d expected %d)", len,
                    sizeof(per_key_rgb_type));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &per_key_rgb_type, sizeof(per_key_rgb_type));
        if (err <= 0) {
            LOG_ERR("Failed to read per_key_rgb_type  from settings (err %d)", err);
            return err;
        }

    } else if (settings_name_steq(name, "per_key_led", NULL)) {
        if (len != sizeof(per_key_led)) {
            LOG_ERR("Invalid per_key_led size (got %d expected %d)", len, sizeof(per_key_led));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, per_key_led, sizeof(per_key_led));
        if (err <= 0) {
            LOG_ERR("Failed to read per_key_led  from settings (err %d)", err);
            return err;
        }

    } else if (settings_name_steq(name, "regions", NULL)) {
        if (len != sizeof(regions)) {
            LOG_ERR("Invalid regions size (got %d expected %d)", len, sizeof(regions));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, regions, sizeof(regions));
        if (err <= 0) {
            LOG_ERR("Failed to read regions  from settings (err %d)", err);
            return err;
        }

    } else if (settings_name_steq(name, "effect_list", NULL)) {
        if (len != sizeof(effect_list)) {
            LOG_ERR("Invalid effect_list size (got %d expected %d)", len, sizeof(effect_list));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, effect_list, sizeof(effect_list));
        if (err <= 0) {
            LOG_ERR("Failed to read effect_list  from settings (err %d)", err);
            return err;
        }

    }

    else if (settings_name_steq(name, "retail_demo_enable", NULL)) {
        if (len != sizeof(retail_demo_enable)) {
            LOG_ERR("Invalid retail_demo_enable size (got %d expected %d)", len,
                    sizeof(retail_demo_enable));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, &retail_demo_enable, sizeof(retail_demo_enable));
        if (err <= 0) {
            LOG_ERR("Failed to read retail_demo_enable  from settings (err %d)", err);
            return err;
        }

    }
#ifdef CONFIG_SNAP_CLICK_ENABLE
    else if (settings_name_steq(name, "snap_click_pair", NULL)) {
        if (len != sizeof(snap_click_pair)) {
            LOG_ERR("Invalid retail_demo_enable size (got %d expected %d)", len,
                    sizeof(snap_click_pair));
            memset(snap_click_pair, 0, sizeof(snap_click_pair));
            return -EINVAL;
        }

        int err = read_cb(cb_arg, snap_click_pair, sizeof(snap_click_pair));
        if (err <= 0) {
            LOG_ERR("Failed to read retail_demo_enable  from settings (err %d)", err);
            return err;
        }
    }
#endif
    return 0;
}

SYS_INIT(eeconfig_init_custom_rgb, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);