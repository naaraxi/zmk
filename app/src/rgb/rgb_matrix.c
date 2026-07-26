
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include "rgb_matrix.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include "rgb_matrix_config.h"
#ifdef CONFIG_ZMK_SSD1306
#include <zmk/disp.h>
#endif

#include <zmk/leds.h>
#include <zmk/usb.h>

#ifdef CONFIG_KEYCHRON_RGB_ENABLE
uint8_t rgb_regions[RGB_MATRIX_LED_COUNT];
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
bool led_eff_indicators(effect_params_t *params);
void save_rgb_matrix_config(void);
void zmk_rgb_task_sync(void);
#ifdef CONFIG_SHIELD_KEYCHRON_V0_ULTRA_ANSI
const led_point_t k_rgb_matrix_center = {36, 32};
#else
const led_point_t k_rgb_matrix_center = {112, 32};
#endif

RGB hsv_to_rgb(HSV hsv) {
    RGB rgb;
    uint8_t region, remainder, p, q, t;
    uint16_t h, s, v;

    if (hsv.s == 0) {

        rgb.r = hsv.v;
        rgb.g = hsv.v;
        rgb.b = hsv.v;

        return rgb;
    }

    h = hsv.h;
    s = hsv.s;
    v = hsv.v;

    region = h * 6 / 255;
    remainder = (h * 2 - region * 85) * 3;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
    case 6:
    case 0:
        rgb.r = v;
        rgb.g = t;
        rgb.b = p;
        break;
    case 1:
        rgb.r = q;
        rgb.g = v;
        rgb.b = p;
        break;
    case 2:
        rgb.r = p;
        rgb.g = v;
        rgb.b = t;
        break;
    case 3:
        rgb.r = p;
        rgb.g = q;
        rgb.b = v;
        break;
    case 4:
        rgb.r = t;
        rgb.g = p;
        rgb.b = v;
        break;
    default:
        rgb.r = v;
        rgb.g = p;
        rgb.b = q;
        break;
    }

    return rgb;
}
__attribute__((weak)) RGB rgb_matrix_hsv_to_rgb(HSV hsv) { return hsv_to_rgb(hsv); }

// globals
rgb_config_t rgb_matrix_config;
uint32_t g_rgb_timer;

uint8_t g_rgb_frame_buffer[MATRIX_ROWS][MATRIX_COLS] = {{0}};

last_hit_t g_last_hit_tracker;

static bool suspend_state = false;
static uint8_t rgb_last_enable = UINT8_MAX;
static uint8_t rgb_last_effect = UINT8_MAX;
static effect_params_t rgb_effect_params = {0, LED_FLAG_ALL, false};
rgb_task_states rgb_task_state = RGB_STATE_SYNC;

static uint32_t rgb_timer_bak;

// --- OpenRGB direct control (ZMK issue #893) -------------------------------
// While active, zmk_rgb_task_render() is bypassed so the colors the host writes
// via zmk_rgb_matrix_set_color() are kept and flushed unchanged.
void os_state_indicate(void); // lock (caps/num) indicator overlay, defined in keychron_rgb.c
static bool rgb_openrgb_direct = false;
static rgb_config_t openrgb_saved_config;

static void openrgb_handback_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(openrgb_handback_work, openrgb_handback_cb);
#define OPENRGB_HANDBACK_MS 3000

bool zmk_rgb_matrix_openrgb_active(void) { return rgb_openrgb_direct; }

void zmk_rgb_matrix_openrgb_enter(void) {
    if (!rgb_openrgb_direct) {
        // Snapshot the current onboard lighting so we can restore it on hand-back.
        openrgb_saved_config = rgb_matrix_config;
        rgb_openrgb_direct = true;
    }

    // Keep the LEDs powered; leave mode untouched so the effect-transition
    // machinery isn't triggered (render is bypassed anyway).
    //
    // Both lines below must run on EVERY enter, not just the first one, so that
    // direct mode survives the host suspending and resuming:
    //   - On USB suspend, rgb_underglow_event_listener() calls
    //     zmk_rgb_matrix_off(), which zeroes rgb_matrix_config.enable *and*
    //     does k_sem_reset(&thread_wait_sem).
    //   - The RGB thread then hits its disabled check, calls zmk_rgb_sleep() to
    //     put the LED driver to sleep, and parks in
    //     k_sem_take(&thread_wait_sem, K_FOREVER).
    //   - Nothing on the resume path reliably calls zmk_rgb_matrix_on() again
    //     (that listener's USB branch is gated on rgb_onoff_status and
    //     usb_configured), so the thread stays blocked forever. The raw-HID
    //     thread is separate and keeps answering GET_LED_COUNT, and set_leds
    //     keeps filling the colour buffer, but nothing ever flushes it and the
    //     driver stays asleep - the LEDs are dark and even the onboard effects
    //     do not run. Measured: only a power cycle recovered it; a USB bus
    //     reset (USBDEVFS_RESET) did not.
    //
    // Setting .enable on its own is NOT enough - it does not wake a thread that
    // is already blocked on the semaphore. k_sem_give is harmless when the
    // thread is running, since thread_wait_sem is defined with a max count of 1
    // so repeated gives saturate.
    //
    // Gate on USB not being suspended, or we resurrect the LEDs during host
    // sleep: a keepalive packet that lands after zmk_rgb_matrix_off() but before
    // the RGB thread reaches its parking branch would re-enable and re-arm the
    // thread, and nothing would switch it off again because the USB state change
    // that triggered off() has already been handled. The keyboard would then stay
    // lit for the whole time the host is asleep. Once the host resumes, the bus is
    // active again by definition (a packet arrived), so recovery still works.
    if (zmk_usb_get_status() != USB_DC_SUSPEND) {
        rgb_matrix_config.enable = 1;
        enable_rgb_thread();
    }

    k_work_reschedule(&openrgb_handback_work, K_MSEC(OPENRGB_HANDBACK_MS));
}

void zmk_rgb_matrix_openrgb_exit(void) {
    if (rgb_openrgb_direct) {
        rgb_openrgb_direct = false;
        // Graceful hand-back: restore the exact onboard effect/color/brightness.
        rgb_matrix_config = openrgb_saved_config;
        rgb_last_effect = 0; // force the restored effect to re-init cleanly
    }
    k_work_cancel_delayable(&openrgb_handback_work);
}

void zmk_rgb_matrix_openrgb_feed(void) {
    if (rgb_openrgb_direct) {
        k_work_reschedule(&openrgb_handback_work, K_MSEC(OPENRGB_HANDBACK_MS));
    }
}

static void openrgb_handback_cb(struct k_work *work) {
    // No OpenRGB frames for OPENRGB_HANDBACK_MS (host quit / USB unplug / switch
    // to BLE or 2.4GHz) -> return control to the onboard effect.
    zmk_rgb_matrix_openrgb_exit();
}
// ---------------------------------------------------------------------------

static last_hit_t last_hit_buffer;

uint8_t rgb_onoff_status;

__attribute__((weak)) uint8_t zmk_rgb_matrix_map_row_column_to_led_kb(uint8_t row, uint8_t column,
                                                                      uint8_t *led_i) {
    return 0;
}

uint8_t zmk_rgb_matrix_map_row_column_to_led(uint8_t row, uint8_t column, uint8_t *led_i) {
    uint8_t led_count = zmk_rgb_matrix_map_row_column_to_led_kb(row, column, led_i);
    uint8_t led_index = g_led_config.matrix_co[row][column];
    if (led_index != NO_LED) {
        led_i[led_count] = led_index;
        led_count++;
    }
    return led_count;
}

void process_rgb_matrix(uint8_t row, uint8_t col, bool pressed) {

    uint8_t led[LED_HITS_TO_REMEMBER];
    uint8_t led_count = 0;

    if ((row >= MATRIX_ROWS)
#ifdef ENCODER_SKIP_MASK
        || (encoder_skip_mask[row] & (1u << col))
#endif // skip encoder key press
    )
        return;

    if (pressed) {
        led_count = zmk_rgb_matrix_map_row_column_to_led(row, col, led);
    }

    if (last_hit_buffer.count + led_count > LED_HITS_TO_REMEMBER) {
        memcpy(&last_hit_buffer.x[0], &last_hit_buffer.x[led_count],
               LED_HITS_TO_REMEMBER - led_count);
        memcpy(&last_hit_buffer.y[0], &last_hit_buffer.y[led_count],
               LED_HITS_TO_REMEMBER - led_count);
        memcpy(&last_hit_buffer.tick[0], &last_hit_buffer.tick[led_count],
               (LED_HITS_TO_REMEMBER - led_count) * 2); // 16 bit
        memcpy(&last_hit_buffer.index[0], &last_hit_buffer.index[led_count],
               LED_HITS_TO_REMEMBER - led_count);
        last_hit_buffer.count = LED_HITS_TO_REMEMBER - led_count;
    }

    for (uint8_t i = 0; i < led_count; i++) {
        uint8_t index = last_hit_buffer.count;
        last_hit_buffer.x[index] = g_led_config.point[led[i]].x;
        last_hit_buffer.y[index] = g_led_config.point[led[i]].y;
        last_hit_buffer.index[index] = led[i];
        last_hit_buffer.tick[index] = 0;
        last_hit_buffer.count++;
    }

#ifdef ENABLE_RGB_EFFECT_TYPING_HEATMAP
    void process_rgb_matrix_typing_heatmap(uint8_t row, uint8_t col);
    if (pressed) {
        if (rgb_matrix_config.mode == RGB_EFFECT_TYPING_HEATMAP) {
            process_rgb_matrix_typing_heatmap(row, col);
        }
    }
#endif
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
    void process_rgb_matrix_kb(uint8_t row, uint8_t col, bool pressed);
    process_rgb_matrix_kb(row, col, pressed);
#endif
}

void zmk_rgb_matrix_test(void) {
#if 0
    uint8_t factor = 10;
    switch ((g_rgb_timer & (0b11 << factor)) >> factor) {
        case 0: {
            zmk_rgb_matrix_set_color_all(20, 0, 0);
            break;
        }
        case 1: {
            zmk_rgb_matrix_set_color_all(0, 20, 0);
            break;
        }
        case 2: {
            zmk_rgb_matrix_set_color_all(0, 0, 20);
            break;
        }
        case 3: {
            zmk_rgb_matrix_set_color_all(20, 20, 20);
            break;
        }
    }
#endif
}

static void zmk_rgb_task_timers(void) {

    uint32_t deltaTime = k_uptime_delta_32(rgb_timer_bak);
    rgb_timer_bak = k_uptime_get_32();

    uint8_t count = last_hit_buffer.count;
    for (uint8_t i = 0; i < count; ++i) {
        if (UINT16_MAX - deltaTime < last_hit_buffer.tick[i]) {
            last_hit_buffer.count--;
            continue;
        }
        last_hit_buffer.tick[i] += deltaTime;
    }
}

// static void zmk_rgb_task_sync(void) {
//     // next task
//     uint32_t deltaTime = k_uptime_delta_32(g_rgb_timer);
//     if ( deltaTime>= RGB_MATRIX_LED_FLUSH_LIMIT) rgb_task_state = RGB_STATE_START;
// }

static void zmk_rgb_task_start(void) {

    rgb_effect_params.iter = 0;

    g_rgb_timer = rgb_timer_bak;

    g_last_hit_tracker = last_hit_buffer;

    rgb_task_state = RGB_STATE_RENDER;
}

static void zmk_rgb_task_render(uint8_t effect) {
    bool rendering = false;

    // OpenRGB direct mode: the host owns the LED buffer. Skip all effect
    // rendering so its colors persist, and just flush what it set. Suspend
    // still wins (LEDs off on sleep to save battery).
    if (rgb_openrgb_direct && !suspend_state) {
        rgb_task_state = RGB_STATE_FLUSH;
        return;
    }

    rgb_effect_params.init =
        (effect != rgb_last_effect) || (rgb_matrix_config.enable != rgb_last_enable);

    if (effect != rgb_last_effect) {
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
        memset(rgb_regions, 0, RGB_MATRIX_LED_COUNT);
#endif
        LOG_ERR("eff:%d,last eff:%d", effect, rgb_last_effect);
        rgb_last_effect = effect;
    }

    if (rgb_effect_params.flags != rgb_matrix_config.flags) {
        // force reset flags! why lost?
        if (rgb_matrix_config.flags == 0) {
            rgb_matrix_config.flags = LED_FLAG_ALL;
        }
        rgb_effect_params.flags = rgb_matrix_config.flags;
        zmk_rgb_matrix_set_color_all(0, 0, 0);
    }

    if (effect == 0x3f) {
        led_eff_indicators(&rgb_effect_params);
        rgb_task_state = RGB_STATE_FLUSH;
        return;
    } else if (effect < RGB_EFFECT_MAX) /// UINT8_MAX)
    {
        rendering = rgb_effect_funcs[effect](&rgb_effect_params);
    }

    else {
        zmk_rgb_matrix_test();
        rgb_task_state = RGB_STATE_FLUSH;
        return;
    }

    rgb_effect_params.iter++;

    if (!rendering) {
        rgb_task_state = RGB_STATE_FLUSH;
        if (!rgb_effect_params.init && effect == RGB_EFFECT_NONE) {
            rgb_task_state = RGB_STATE_SYNC;
        }
    }
}

static void zmk_rgb_task_flush(uint8_t effect) {
    // rgb_last_effect = effect;
    rgb_last_enable = rgb_matrix_config.enable;

    // update pwm buffers
    zmk_rgb_matrix_update_pwm_buffers();

    // next task
    rgb_task_state = RGB_STATE_SYNC;
}
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
rgb_config_t back_rgb_config = {0};
#endif
void zmk_rgb_matrix_task(void) {
    zmk_rgb_task_timers();

    // Ideally we would also stop sending zeros to the LED driver PWM buffers
    // while suspended and just do a software shutdown. This is a cheap hack for now.
    bool suspend_backlight = suspend_state || false;

    uint8_t effect =
        (suspend_backlight || (!rgb_matrix_config.enable && !rgb_led_indicators.rgb_enable))
            ? 0
            : rgb_matrix_config.mode;

    static uint8_t back_effect = 0;
    if (back_effect != effect) {
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
        // restore rgb config when switch from mixed rgb!
        if (effect == RGB_EFFECT_MIXED_RGB) {
            if (back_rgb_config.raw == 0) {
                back_rgb_config = rgb_matrix_config;
                LOG_ERR("back mode:%d,hsv:%x,%x,%x", rgb_matrix_config.mode,
                        rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s, rgb_matrix_config.hsv.v);
            }
        } else if (back_effect == RGB_EFFECT_MIXED_RGB) {
            if ((effect > 0 && effect != 0x3f) && (back_rgb_config.raw != 0)) {
                back_rgb_config.hsv.v = rgb_matrix_config.hsv.v;
                rgb_matrix_config = back_rgb_config;
                back_rgb_config.raw = 0;
                rgb_matrix_config.mode = effect;
                rgb_matrix_config.back_mode = effect;
                LOG_ERR("restore mode:%d,hsv:%x,%x,%x", rgb_matrix_config.mode,
                        rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s, rgb_matrix_config.hsv.v);
            }
            rgb_matrix_config.enable = effect != 0;
        }
#endif
        back_effect = effect;
        rgb_last_effect = 0;
        LOG_ERR("change effect:%d,back:%d,last:%d,enable:%d", effect, back_effect, rgb_last_effect,
                rgb_matrix_config.enable);
    }
    switch (rgb_task_state) {
    case RGB_STATE_START:
        zmk_rgb_task_start();
        break;
    case RGB_STATE_RENDER:
        zmk_rgb_task_render(effect);
        if (rgb_openrgb_direct) {
            // OpenRGB owns the keys, but overlay the OS lock indicators (caps/num)
            // on top so lock state stays visible while the host drives the rest.
            os_state_indicate();
        } else if (effect) { // don't let full indicators stomp host colors
            if (rgb_task_state == RGB_STATE_FLUSH) { // ensure we only draw basic indicators once
                                                     // rendering is finished
                zmk_rgb_matrix_indicators();
            }
            zmk_rgb_matrix_indicators_advanced(&rgb_effect_params);
        }
        break;
    case RGB_STATE_FLUSH:
        zmk_rgb_task_flush(effect);
        break;
    case RGB_STATE_SYNC:
        zmk_rgb_task_sync();
        break;
    }
}

void os_state_indicate(void);
extern uint8_t fn_win_lock;
uint8_t winlock_led_is_on(void);
void zmk_rgb_matrix_indicators(void) {
    // zmk_rgb_matrix_indicators_kb();
    // if(keyboard_get_led_state()&0x02)
    // {
    //     if(CAPS_LOCK_INDEX!=0xff)
    //     zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX, 255, 255, 255);
    // }
    // if(keyboard_get_led_state()&0x01)
    // {
    //     if(NUM_LOCK_INDEX!=0xff)
    //     zmk_rgb_matrix_set_color(NUM_LOCK_INDEX, 255, 255, 255);
    // }
    os_state_indicate();
#ifdef CONFIG_ENABLE_WIN_LOCK_INDICATOR

    if (fn_win_lock && winlock_led_is_on()) {
        if (FN_WIN_LOCK_INDEX != 0xff)
            zmk_rgb_matrix_set_color(FN_WIN_LOCK_INDEX, 0, 0, 0);
    }
#endif
}
bool zmk_rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    // if(keyboard_get_led_state()&0x02)
    // {
    //     // LOG_ERR("led min:%d,max:%d",led_min,led_max);
    //     if(CAPS_LOCK_INDEX!=0xff)
    //     RGB_MATRIX_INDICATOR_SET_COLOR(CAPS_LOCK_INDEX, 255, 255, 255);

    // }
    // if(keyboard_get_led_state()&0x01)
    // {
    //     if(NUM_LOCK_INDEX!=0xff)
    //     zmk_rgb_matrix_set_color(NUM_LOCK_INDEX, 255, 255, 255);
    // }
    // #ifdef CONFIG_ENABLE_WIN_LOCK
    //     if(fn_win_lock)
    //     {
    //         if(FN_WIN_LOCK_INDEX!=0xff)
    //         zmk_rgb_matrix_set_color(FN_WIN_LOCK_INDEX, 0, 0, 0);
    //     }
    // #endif
    return false;
}
struct rgb_matrix_limits_t zmk_rgb_matrix_get_limits(uint8_t iter) {
    struct rgb_matrix_limits_t limits = {0};
#if defined(RGB_MATRIX_LED_PROCESS_LIMIT) && RGB_MATRIX_LED_PROCESS_LIMIT > 0 &&                   \
    RGB_MATRIX_LED_PROCESS_LIMIT < RGB_MATRIX_LED_COUNT

    limits.led_min_index = RGB_MATRIX_LED_PROCESS_LIMIT * (iter);
    limits.led_max_index = limits.led_min_index + RGB_MATRIX_LED_PROCESS_LIMIT;
    if (limits.led_max_index > RGB_MATRIX_LED_COUNT)
        limits.led_max_index = RGB_MATRIX_LED_COUNT;

#else

    limits.led_min_index = 0;
    limits.led_max_index = RGB_MATRIX_LED_COUNT;

#endif
    return limits;
}

void zmk_rgb_matrix_indicators_advanced(effect_params_t *params) {
    /* special handling is needed for "params->iter", since it's already been incremented.
     * Could move the invocations to rgb_task_render, but then it's missing a few checks
     * and not sure which would be better. Otherwise, this should be called from
     * rgb_task_render, right before the iter++ line.
     */
    RGB_MATRIX_USE_LIMITS_ITER(min, max, params->iter - 1);
    zmk_rgb_matrix_indicators_advanced_kb(min, max);
}

int zmk_rgb_matrix_init(void) {
    zmk_rgb_matrix_driver_init();

    g_last_hit_tracker.count = 0;
    for (uint8_t i = 0; i < LED_HITS_TO_REMEMBER; ++i) {
        g_last_hit_tracker.tick[i] = UINT16_MAX;
    }

    last_hit_buffer.count = 0;
    for (uint8_t i = 0; i < LED_HITS_TO_REMEMBER; ++i) {
        last_hit_buffer.tick[i] = UINT16_MAX;
    }

    return 0;
}

void zmk_rgb_matrix_set_suspend_state(bool state) {

    if (state && !suspend_state) {
        zmk_rgb_task_render(0);
        zmk_rgb_task_flush(0);
    }
    suspend_state = state;
}

bool zmk_rgb_matrix_get_suspend_state(void) { return suspend_state; }
void zmk_rgb_matrix_toggle_no_save(void) {
    rgb_matrix_config.enable ^= 1;
    rgb_task_state = RGB_STATE_START;
    // save_rgb_matrix_config();
    LOG_DBG("rgb matrix toggle:enable = %u,mode:%d,%d\n", rgb_matrix_config.enable,
            rgb_matrix_config.mode, rgb_matrix_config.back_mode);
    if (rgb_matrix_config.enable) {
        if (rgb_matrix_config.hsv.v == 0)
            rgb_matrix_config.hsv.v = RGB_MATRIX_VAL_STEP;
    } else {
        if (rgb_matrix_config.mode != 0x3f)
            rgb_matrix_config.back_mode = rgb_matrix_config.mode;
    }
    rgb_onoff_status = rgb_matrix_config.enable;
}
void zmk_rgb_matrix_toggle(void) {
    // rgb_matrix_config.enable ^= 1;
    // rgb_task_state = RGB_STATE_START;
    // save_rgb_matrix_config();
    // LOG_DBG("rgb matrix toggle:enable = %u\n",rgb_matrix_config.enable);
    // rgb_onoff_status = rgb_matrix_config.enable;
    zmk_rgb_matrix_toggle_no_save();
    save_rgb_matrix_config_rightnow();
}

void zmk_rgb_matrix_enable(void) {
    if (!rgb_matrix_config.enable)
        rgb_task_state = RGB_STATE_START;
    rgb_matrix_config.enable = 1;
    save_rgb_matrix_config();
    rgb_onoff_status = 1;
}

void zmk_rgb_matrix_disable(void) {
    if (rgb_matrix_config.enable)
        rgb_task_state = RGB_STATE_START;
    rgb_matrix_config.enable = 0;
    save_rgb_matrix_config();
    rgb_onoff_status = 0;
}

uint8_t zmk_rgb_matrix_is_enabled(void) { return rgb_matrix_config.enable; }
void zmk_rgb_matrix_mode_no_save(uint8_t mode) {

    if (mode < 1) {
        rgb_matrix_config.mode = 1;
    } else if (mode >= RGB_EFFECT_MAX) {
        rgb_matrix_config.mode = RGB_EFFECT_MAX - 1;
    } else {
        rgb_matrix_config.mode = mode;
    }
    rgb_task_state = RGB_STATE_START;
#ifdef CONFIG_ZMK_SSD1306
    disp_change_setting(TYPE_LIGHTMODE, rgb_matrix_config.mode);
#endif
    // save_rgb_matrix_config();
    LOG_DBG("rgb matrix mode: %u\n", rgb_matrix_config.mode);
    rgb_matrix_config.back_mode = rgb_matrix_config.mode;
    // rgb_led_indicators.rgb_mode =rgb_matrix_config.mode;
}
void zmk_rgb_matrix_mode(uint8_t mode) {
    // if (!rgb_matrix_config.enable) {
    //     return;
    // }
    // if (mode < 1) {
    //     rgb_matrix_config.mode = 1;
    // } else if (mode >= RGB_EFFECT_MAX) {
    //     rgb_matrix_config.mode = RGB_EFFECT_MAX - 1;
    // } else {
    //     rgb_matrix_config.mode = mode;
    // }
    // rgb_task_state = RGB_STATE_START;
    // save_rgb_matrix_config();
    // LOG_DBG("rgb matrix mode: %u\n", rgb_matrix_config.mode);
    // rgb_led_indicators.rgb_mode =rgb_matrix_config.mode;
    zmk_rgb_matrix_mode_no_save(mode);
    if (rgb_matrix_config.mode < RGB_EFFECT_MAX)
        save_rgb_matrix_config_rightnow();
}

uint8_t zmk_rgb_matrix_get_mode(void) { return rgb_matrix_config.mode; }

void zmk_rgb_matrix_step(void) {
    uint8_t mode;
    if (!rgb_matrix_config.enable) {
        return;
    }
    if (rgb_matrix_config.mode == 0x3f) {
        mode = rgb_matrix_config.back_mode + 1;
    } else
        mode = rgb_matrix_config.mode + 1;
    LOG_DBG("mode:%d", mode);
    zmk_rgb_matrix_mode((mode < RGB_EFFECT_MAX) ? mode : 1);
}

void zmk_rgb_matrix_step_reverse(void) {
    uint8_t mode;
    if (!rgb_matrix_config.enable) {
        return;
    }
    if (rgb_matrix_config.mode == 0x3f) {
        mode = rgb_matrix_config.back_mode - 1;
    } else
        mode = rgb_matrix_config.mode - 1;

    LOG_DBG("mode:%d", mode);
    zmk_rgb_matrix_mode((mode < 1) ? RGB_EFFECT_MAX - 1 : mode);
}
void zmk_rgb_matrix_sethsv_no_save(uint16_t hue, uint8_t sat, uint8_t val) {
    if (!rgb_matrix_config.enable) {
        return;
    }
    rgb_matrix_config.hsv.h = hue;
    rgb_matrix_config.hsv.s = sat;
    rgb_matrix_config.hsv.v =
        (val > RGB_MATRIX_MAXIMUM_BRIGHTNESS) ? RGB_MATRIX_MAXIMUM_BRIGHTNESS : val;
    // save_rgb_matrix_config();
    LOG_DBG("rgb matrix set hsv: %u,%u,%u\n", rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s,
            rgb_matrix_config.hsv.v);
}
void zmk_rgb_matrix_sethsv(uint16_t hue, uint8_t sat, uint8_t val) {
    // if (!rgb_matrix_config.enable) {
    //     return;
    // }
    // rgb_matrix_config.hsv.h = hue;
    // rgb_matrix_config.hsv.s = sat;
    // rgb_matrix_config.hsv.v = (val > RGB_MATRIX_MAXIMUM_BRIGHTNESS) ?
    // RGB_MATRIX_MAXIMUM_BRIGHTNESS : val; save_rgb_matrix_config(); LOG_DBG("rgb matrix set hsv:
    // %u,%u,%u\n",rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s, rgb_matrix_config.hsv.v);
    zmk_rgb_matrix_sethsv_no_save(hue, sat, val);
    save_rgb_matrix_config();
}

HSV zmk_rgb_matrix_get_hsv(void) { return rgb_matrix_config.hsv; }
uint8_t zmk_rgb_matrix_get_hue(void) { return rgb_matrix_config.hsv.h; }
uint8_t zmk_rgb_matrix_get_sat(void) { return rgb_matrix_config.hsv.s; }
uint8_t zmk_rgb_matrix_get_val(void) { return rgb_matrix_config.hsv.v; }

void zmk_rgb_matrix_increase_hue(void) {
    zmk_rgb_matrix_sethsv(rgb_matrix_config.hsv.h + RGB_MATRIX_HUE_STEP, rgb_matrix_config.hsv.s,
                          rgb_matrix_config.hsv.v);
}

void zmk_rgb_matrix_decrease_hue(void) {
    zmk_rgb_matrix_sethsv(rgb_matrix_config.hsv.h - RGB_MATRIX_HUE_STEP, rgb_matrix_config.hsv.s,
                          rgb_matrix_config.hsv.v);
}

void zmk_rgb_matrix_increase_sat(void) {
    zmk_rgb_matrix_sethsv(rgb_matrix_config.hsv.h,
                          qadd8(rgb_matrix_config.hsv.s, RGB_MATRIX_SAT_STEP),
                          rgb_matrix_config.hsv.v);
}

void zmk_rgb_matrix_decrease_sat(void) {
    zmk_rgb_matrix_sethsv(rgb_matrix_config.hsv.h,
                          qsub8(rgb_matrix_config.hsv.s, RGB_MATRIX_SAT_STEP),
                          rgb_matrix_config.hsv.v);
}

void zmk_rgb_matrix_increase_val(void) {
    zmk_rgb_matrix_sethsv(rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s,
                          qadd8(rgb_matrix_config.hsv.v, RGB_MATRIX_VAL_STEP));
#ifdef CONFIG_ZMK_SSD1306
    uint8_t index = (rgb_matrix_config.hsv.v + RGB_MATRIX_VAL_STEP / 2) / RGB_MATRIX_VAL_STEP;
    disp_change_setting(TYPE_LIGHTNESS, index);
#endif
}

void zmk_rgb_matrix_decrease_val(void) {
    zmk_rgb_matrix_sethsv(rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s,
                          qsub8(rgb_matrix_config.hsv.v, RGB_MATRIX_VAL_STEP));
#ifdef CONFIG_ZMK_SSD1306
    uint8_t index = (rgb_matrix_config.hsv.v + RGB_MATRIX_VAL_STEP / 2) / RGB_MATRIX_VAL_STEP;
    disp_change_setting(TYPE_LIGHTNESS, index);
#endif
}
void zmk_rgb_matrix_set_speed_no_save(uint8_t speed) {
    if (!rgb_matrix_config.enable) {
        return;
    }
    rgb_matrix_config.speed = speed;
#ifdef CONFIG_ZMK_SSD1306
    if (rgb_matrix_config.mode != RGB_EFFECT_MIXED_RGB) {
        uint8_t index = (rgb_matrix_config.speed + RGB_MATRIX_SPD_STEP / 2) / RGB_MATRIX_SPD_STEP;
        disp_change_setting(TYPE_LIGHTSPEED, index);
    }
#endif

    LOG_DBG("rgb matrix set speed: %u\n", rgb_matrix_config.speed);
}
void zmk_rgb_matrix_set_speed(uint8_t speed) {
    zmk_rgb_matrix_set_speed_no_save(speed);
    save_rgb_matrix_config();
    LOG_DBG("rgb matrix set speed: %u\n", rgb_matrix_config.speed);
}

uint8_t zmk_rgb_matrix_get_speed(void) { return rgb_matrix_config.speed; }

void zmk_rgb_matrix_increase_speed(void) {
    zmk_rgb_matrix_set_speed(qadd8(rgb_matrix_config.speed, RGB_MATRIX_SPD_STEP));
}

void zmk_rgb_matrix_decrease_speed(void) {
    zmk_rgb_matrix_set_speed(qsub8(rgb_matrix_config.speed, RGB_MATRIX_SPD_STEP));
}

led_flags_t zmk_rgb_matrix_get_flags(void) { return rgb_matrix_config.flags; }

void zmk_rgb_matrix_set_flags(led_flags_t flags) {
    rgb_matrix_config.flags = flags;
    save_rgb_matrix_config();
    LOG_DBG("rgb matrix set flags : %u\n", rgb_matrix_config.flags);
}
void zmk_rgb_matrix_set_state(uint8_t state) { rgb_task_state = state; }
// uint8_t zmk_rgb_get_onoff_status(void)
// {
//     return rgb_onoff_status;
// }
SYS_INIT(zmk_rgb_matrix_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#ifndef CONFIG_KEYCHRON_RGB_ENABLE
void os_state_indicate(void) {

    RGB rgb = {0xff, 0xff, 0xff};

#if defined(NUM_LOCK_INDEX)
    if (keyboard_get_led_state().num_lock) {
        zmk_rgb_matrix_set_color(NUM_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(CAPS_LOCK_INDEX)
    if (keyboard_get_led_state().caps_lock) {
        zmk_rgb_matrix_set_color(CAPS_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(SCROLL_LOCK_INDEX)
    if (keyboard_get_led_state().scroll_lock) {
        zmk_rgb_matrix_set_color(SCROLL_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(COMPOSE_LOCK_INDEX)
    if (keyboard_get_led_state().compose) {
        zmk_rgb_matrix_set_color(COMPOSE_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
#if defined(KANA_LOCK_INDEX)
    if (keyboard_get_led_state().kana) {
        zmk_rgb_matrix_set_color(KANA_LOCK_INDEX, rgb.r, rgb.g, rgb.b);
    }
#endif
    (void)rgb;
}
#endif