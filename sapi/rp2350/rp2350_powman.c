#include "rp2350_powman.h"

#include <math.h>

#include "boards/pimoroni_tufty2350.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "hardware/powman.h"

static uint8_t s_wake_reason = 0;

#define LED_PEAK_BRIGHTNESS 150
#define LED_IN_PHASE 30
#define LED_OUT_PHASE 0
#define LED_TOTAL (LED_PEAK_BRIGHTNESS + LED_IN_PHASE * 3)
#define LED_FADEOUT_SPEED 5
#define LED_GAMMA 1.8f
#define LED_DELAY_MS 5
#define LED_ON_DELAY_MS 200
#define LED_ON_DELAY (LED_ON_DELAY_MS / LED_DELAY_MS)

static const uint s_led_gpios[4] = {BW_LED_1, BW_LED_2, BW_LED_3, BW_LED_0};

static void rp2350_powman_setup_buttons_only(void) {
    gpio_init_mask(BW_SWITCH_MASK);
    gpio_set_dir_in_masked(BW_SWITCH_MASK);
    gpio_set_pulls(BW_SWITCH_A, true, false);
    gpio_set_pulls(BW_SWITCH_B, true, false);
    gpio_set_pulls(BW_SWITCH_C, true, false);
    gpio_set_pulls(BW_SWITCH_UP, true, false);
    gpio_set_pulls(BW_SWITCH_DOWN, true, false);

    gpio_init(BW_SWITCH_INT);
    gpio_set_dir(BW_SWITCH_INT, GPIO_IN);
    gpio_set_pulls(BW_SWITCH_INT, true, false);

    gpio_init(BW_RTC_ALARM);
    gpio_set_dir(BW_RTC_ALARM, GPIO_IN);
    gpio_set_pulls(BW_RTC_ALARM, true, false);

    gpio_init(BW_RESET_SW);
    gpio_set_dir(BW_RESET_SW, GPIO_IN);
    gpio_pull_up(BW_RESET_SW);
}

static void rp2350_powman_disable_led_pwm(void) {
    for (unsigned i = 0; i < 4; i++) {
        pwm_set_enabled(pwm_gpio_to_slice_num(s_led_gpios[i]), false);
    }
    gpio_init_mask((1u << BW_LED_0) | (1u << BW_LED_1) | (1u << BW_LED_2) | (1u << BW_LED_3));
    gpio_set_dir_out_masked((1u << BW_LED_0) | (1u << BW_LED_1) | (1u << BW_LED_2) | (1u << BW_LED_3));
    gpio_put_masked((1u << BW_LED_0) | (1u << BW_LED_1) | (1u << BW_LED_2) | (1u << BW_LED_3), 0);
}

void rp2350_powman_early_init(void) {
    gpio_init(BW_SW_POWER_EN);
    gpio_set_dir(BW_SW_POWER_EN, GPIO_OUT);
    gpio_put(BW_SW_POWER_EN, 1);

    gpio_init(BW_RESET_SW);
    gpio_set_dir(BW_RESET_SW, GPIO_IN);
    gpio_pull_up(BW_RESET_SW);

    s_wake_reason = (uint8_t)(powman_hw->last_swcore_pwrup & 0x7f);
}

void rp2350_powman_after_wake_init(void) {
    gpio_init(BW_SW_POWER_EN);
    gpio_set_dir(BW_SW_POWER_EN, GPIO_OUT);
    gpio_put(BW_SW_POWER_EN, 1);
}

int rp2350_powman_sleep(void) {
    powman_power_state off_state = POWMAN_POWER_STATE_NONE;
    powman_power_state on_state = POWMAN_POWER_STATE_NONE;
    bool valid_state;
    int rc;

    rp2350_powman_setup_buttons_only();

    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_SWITCHED_CORE);
    on_state = powman_power_state_with_domain_on(on_state, POWMAN_POWER_DOMAIN_XIP_CACHE);

    valid_state = powman_configure_wakeup_state(off_state, on_state);
    if (!valid_state) {
        return PICO_ERROR_INVALID_STATE;
    }

    powman_enable_gpio_wakeup(1u, BW_RTC_ALARM, true, false);
    powman_enable_gpio_wakeup(3u, BW_SWITCH_INT, true, false);

    rc = powman_set_power_state(off_state);
    if (rc != PICO_OK) {
        return rc;
    }
    while (true) {
        __asm volatile ("wfi");
    }
}

int rp2350_powman_shipping_mode(void) {
    powman_power_state off_state = POWMAN_POWER_STATE_NONE;
    powman_power_state on_state = POWMAN_POWER_STATE_NONE;
    bool valid_state = powman_configure_wakeup_state(off_state, on_state);
    int rc;

    if (!valid_state) {
        return PICO_ERROR_INVALID_STATE;
    }
    rc = powman_set_power_state(off_state);
    if (rc != PICO_OK) {
        return rc;
    }
    while (true) {
        __asm volatile ("wfi");
    }
}

bool rp2350_powman_maybe_handle_long_press(void) {
    pwm_config config;
    int br = 0;

    if (gpio_get(BW_RESET_SW) != 0) {
        return false;
    }

    config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / 2048.0f);
    pwm_config_set_wrap(&config, 1024);
    for (unsigned i = 0; i < 4; i++) {
        gpio_set_function(s_led_gpios[i], GPIO_FUNC_PWM);
        pwm_init(pwm_gpio_to_slice_num(s_led_gpios[i]), &config, true);
    }

    while (true) {
        int cbr;
        int o;
        int phase;
        int level = 0;

        if (gpio_get(BW_RESET_SW) != 0) {
            break;
        }

        cbr = br < LED_ON_DELAY ? 0 : br - LED_ON_DELAY;
        o = cbr >= LED_TOTAL ? LED_TOTAL - (cbr - LED_TOTAL) * LED_FADEOUT_SPEED : cbr;
        phase = cbr >= LED_TOTAL ? LED_OUT_PHASE : LED_IN_PHASE;

        for (unsigned i = 0; i < 4; i++) {
            int v = (int)fmaxf(0.0f, fminf((float)LED_PEAK_BRIGHTNESS, (float)o));
            int gamma = (int)(powf((float)v, LED_GAMMA));
            pwm_set_gpio_level(s_led_gpios[i], gamma);
            level += gamma;
            o -= phase;
        }

        if (cbr > 0 && level == 0) {
            rp2350_powman_disable_led_pwm();
            if (!gpio_get(BW_SWITCH_UP) && !gpio_get(BW_SWITCH_DOWN)) {
                (void)rp2350_powman_shipping_mode();
            } else {
                (void)rp2350_powman_sleep();
            }
            return true;
        }
        br++;
        sleep_ms(LED_DELAY_MS);
    }

    rp2350_powman_disable_led_pwm();
    return false;
}

uint8_t rp2350_powman_wake_reason(void) {
    return s_wake_reason;
}
