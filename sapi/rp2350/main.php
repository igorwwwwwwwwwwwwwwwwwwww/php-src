<?php
require '/lib.php';
require '/logo.php';

function run_pcre_smoke() {
    $m = preg_match('/RP2350/', 'PHP RP2350');
    $r = preg_replace('/\s+/', '-', 'a b c');
    if ($m === 1 && $r === 'a-b-c') {
        print "pcre:ok\n";
    } else {
        print "pcre:fail\n";
    }
}

function run_json_smoke() {
    $s = json_encode(['mcu' => 'rp2350', 'ok' => true]);
    $v = json_decode($s, true);
    if (is_string($s) && is_array($v) && ($v['mcu'] ?? null) === 'rp2350' && ($v['ok'] ?? null) === true) {
        print "json:ok\n";
    } else {
        print "json:fail\n";
    }
}

function run_hash_smoke() {
    $h = hash('sha256', 'rp2350');
    if ($h === '4edf24a8528802ecb76196343bd5e677dd5dd56494cc8fbafbd1025791c22fc2') {
        print "hash:ok\n";
    } else {
        print "hash:fail\n";
    }
}

function wifi_status_label($status) {
    if ($status === MCU_WIFI_LINK_UP) {
        return 'UP';
    }
    if ($status === MCU_WIFI_LINK_NOIP) {
        return 'NOIP';
    }
    if ($status === MCU_WIFI_LINK_JOIN) {
        return 'JOIN';
    }
    if ($status === MCU_WIFI_LINK_BADAUTH) {
        return 'AUTH';
    }
    if ($status === MCU_WIFI_LINK_NONET) {
        return 'NONET';
    }
    if ($status === MCU_WIFI_LINK_FAIL) {
        return 'FAIL';
    }
    return 'DOWN';
}

function tft_theme() {
    static $theme = null;
    if ($theme !== null) {
        return $theme;
    }
    $theme = [
        'bg' => mcu_rgb565(7, 12, 20),
        'panel' => mcu_rgb565(18, 28, 40),
        'accent' => mcu_rgb565(0, 180, 220),
        'accent2' => mcu_rgb565(255, 180, 0),
        'fg' => mcu_rgb565(245, 248, 250),
        'muted' => mcu_rgb565(150, 168, 182),
        'ok' => mcu_rgb565(80, 220, 120),
        'warn' => mcu_rgb565(255, 210, 80),
        'clear' => mcu_rgb565(18, 28, 40),
    ];
    return $theme;
}

function tft_status_base_cfb() {
    static $built = false;
    if ($built) {
        return;
    }
    $c = tft_theme();
    mcu_tft_fb_clear($c['bg']);
    mcu_tft_fb_fill_rect(8, 8, MCU_TFT_WIDTH - 16, MCU_TFT_HEIGHT - 16, $c['panel']);
    mcu_tft_fb_fill_rect(8, 8, MCU_TFT_WIDTH - 16, 6, $c['accent']);
    mcu_tft_fb_fill_rect(8, 54, MCU_TFT_WIDTH - 16, 2, $c['accent2']);
    mcu_tft_fb_draw_text(18, 20, 'PHP RP2350', $c['fg'], 3, 2);
    mcu_tft_fb_draw_text(20, 64, 'PHP ' . PHP_VERSION, $c['muted'], 2, 2);
    mcu_tft_fb_draw_text(20, 96, 'BAT', $c['accent2'], 2, 2);
    mcu_tft_fb_draw_text(20, 126, 'WIFI', $c['accent'], 2, 2);
    mcu_tft_fb_draw_text(20, 152, 'LIGHT', $c['accent2'], 1, 1);
    mcu_tft_fb_draw_text(130, 152, 'BL', $c['accent'], 1, 1);
    mcu_tft_fb_draw_text(20, 170, 'UPDATED', $c['muted'], 1, 1);
    mcu_tft_fb_draw_text(20, 188, 'V4', $c['muted'], 1, 1);
    mcu_tft_fb_draw_text(20, 204, 'V6', $c['muted'], 1, 1);
    mcu_tft_fb_draw_text(20, 220, 'UP/DOWN BL  BTN C LOGO', $c['accent2'], 1, 1);
    $built = true;
}

function redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6, $light_raw = 0, $backlight_pct = 100) {
    static $tft_prev = null;
    $render_t0 = microtime(true);
    $has_epd = function_exists('mcu_epd_render');
    $has_tft = function_exists('mcu_tft_render');

    if ($mode_logo) {
        $logo = php_logo_data();
        if ($logo !== false) {
            $buf = $logo[0];
            $w = $logo[1];
            $h = $logo[2];
            if ($has_epd) {
                print "epd:render:logo\n";
                mcu_epd_render($buf, $w, $h);
                print "render_ms:";
                print (int) ((microtime(true) - $render_t0) * 1000.0);
                print "\n";
            } elseif ($has_tft) {
                $color_logo = php_logo_rgb565_data();
                if ($color_logo !== false) {
                    $logo_pixels = $color_logo[0];
                    $logo_w = $color_logo[1];
                    $logo_h = $color_logo[2];
                    $bg = mcu_rgb565(7, 12, 20);
                    $frame = mcu_tft_fb_create(MCU_TFT_WIDTH, MCU_TFT_HEIGHT, $bg);
                    $x = (int)((MCU_TFT_WIDTH - $logo_w) / 2);
                    $y = (int)((MCU_TFT_HEIGHT - $logo_h) / 2);
                    for ($yy = 0; $yy < $logo_h; $yy++) {
                        $src_off = $yy * $logo_w * 2;
                        $dst_off = (($y + $yy) * MCU_TFT_WIDTH + $x) * 2;
                        for ($i = 0; $i < $logo_w * 2; $i++) {
                            $frame[$dst_off + $i] = $logo_pixels[$src_off + $i];
                        }
                    }
                    print "tft:render:logo\n";
                    mcu_tft_render($frame, MCU_TFT_WIDTH, MCU_TFT_HEIGHT);
                    print "render_ms:";
                    print (int) ((microtime(true) - $render_t0) * 1000.0);
                    print "\n";
                } else {
                    $bg = mcu_rgb565(7, 12, 20);
                    $fg = mcu_rgb565(245, 248, 250);
                    $accent = mcu_rgb565(0, 180, 220);
                    $logo_buf = mcu_tft_fb_create(MCU_TFT_WIDTH, MCU_TFT_HEIGHT, $bg);
                    $x = (int)((MCU_TFT_WIDTH - $w) / 2);
                    $y = (int)((MCU_TFT_HEIGHT - $h) / 2) - 12;
                    for ($yy = 0; $yy < $h; $yy++) {
                        for ($xx = 0; $xx < $w; $xx++) {
                            $stride = ($w + 7) >> 3;
                            $idx = ($yy * $stride) + ($xx >> 3);
                            $mask = 1 << (7 - ($xx & 7));
                            if ((ord($buf[$idx]) & $mask) !== 0) {
                                mcu_tft_set_pixel_rgb565($logo_buf, MCU_TFT_WIDTH, MCU_TFT_HEIGHT, $x + $xx, $y + $yy, $fg);
                            }
                        }
                    }
                    mcu_tft_draw_text($logo_buf, MCU_TFT_WIDTH, MCU_TFT_HEIGHT, 96, $y + $h + 18, 'PHP', $accent, null, 3, 2);
                    print "tft:render:logo\n";
                    mcu_tft_render($logo_buf, MCU_TFT_WIDTH, MCU_TFT_HEIGHT);
                    print "render_ms:";
                    print (int) ((microtime(true) - $render_t0) * 1000.0);
                    print "\n";
                }
            }
        }
        return;
    }

    if ($has_epd) {
        $buf = mcu_fb_create(MCU_EPD_WIDTH, MCU_EPD_HEIGHT, false);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 16, 16, 'PHP RP2350', 3, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 16, 54, 'PHP ' . PHP_VERSION, 2, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 16, 78, 'BAT', 2, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 70, 78, (string)$batt_pct, 2, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 114, 78, $usb_connected ? 'USB' : 'BAT', 2, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 164, 78, $charging ? 'CHG' : 'IDLE', 2, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 16, 100, 'WIFI', 2, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 70, 100, wifi_status_label($wifi_status), 2, 2);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 8, 136, 'UPDATED', 1, 1);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 56, 136, date('Y-m-d\\TH:i:s'), 1, 1);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 8, 152, 'V4', 1, 1);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 28, 152, (is_string($wifi_ip4) && $wifi_ip4 !== '') ? $wifi_ip4 : '-', 1, 1);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 8, 164, 'V6', 1, 1);
        mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 28, 164, (is_string($wifi_ip6) && $wifi_ip6 !== '') ? $wifi_ip6 : '-', 1, 1);
        print "epd:render:text\n";
        mcu_epd_render($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT);
        print "render_ms:";
        print (int) ((microtime(true) - $render_t0) * 1000.0);
        print "\n";
        return;
    }

    if ($has_tft) {
        $t0 = microtime(true);
        $c = tft_theme();
        tft_status_base_cfb();
        $t_base = microtime(true);

        $curr = [
            'batt_pct' => (string)$batt_pct . '%',
            'usb' => $usb_connected ? 'USB' : 'BAT',
            'charging' => $charging ? 'CHG' : 'IDLE',
            'wifi' => wifi_status_label($wifi_status),
            'light' => (string)$light_raw,
            'bl' => (string)$backlight_pct . '%',
            'updated' => date('Y-m-d\\TH:i:s'),
            'ip4' => (is_string($wifi_ip4) && $wifi_ip4 !== '') ? $wifi_ip4 : '-',
            'ip6' => (is_string($wifi_ip6) && $wifi_ip6 !== '') ? $wifi_ip6 : '-',
        ];
        $force_all = ($tft_prev === null);

        if ($force_all || $tft_prev['batt_pct'] !== $curr['batt_pct']) {
            mcu_tft_fb_fill_rect(92, 96, 68, 24, $c['panel']);
            mcu_tft_fb_draw_text(92, 96, $curr['batt_pct'], $c['fg'], 2, 2);
        }
        if ($force_all || $tft_prev['usb'] !== $curr['usb']) {
            mcu_tft_fb_fill_rect(180, 96, 52, 24, $c['panel']);
            mcu_tft_fb_draw_text(180, 96, $curr['usb'], $usb_connected ? $c['ok'] : $c['muted'], 2, 2);
        }
        if ($force_all || $tft_prev['charging'] !== $curr['charging']) {
            mcu_tft_fb_fill_rect(240, 96, 56, 24, $c['panel']);
            mcu_tft_fb_draw_text(240, 96, $curr['charging'], $charging ? $c['warn'] : $c['muted'], 2, 2);
        }
        if ($force_all || $tft_prev['wifi'] !== $curr['wifi']) {
            mcu_tft_fb_fill_rect(92, 126, 80, 24, $c['panel']);
            mcu_tft_fb_draw_text(92, 126, $curr['wifi'], $wifi_status === MCU_WIFI_LINK_UP ? $c['ok'] : $c['warn'], 2, 2);
        }
        if ($force_all || $tft_prev['light'] !== $curr['light']) {
            mcu_tft_fb_fill_rect(64, 152, 48, 12, $c['panel']);
            mcu_tft_fb_draw_text(64, 152, $curr['light'], $c['fg'], 1, 1);
        }
        if ($force_all || $tft_prev['bl'] !== $curr['bl']) {
            mcu_tft_fb_fill_rect(150, 152, 40, 12, $c['panel']);
            mcu_tft_fb_draw_text(150, 152, $curr['bl'], $c['fg'], 1, 1);
        }
        if ($force_all || $tft_prev['updated'] !== $curr['updated']) {
            mcu_tft_fb_fill_rect(84, 170, 150, 12, $c['panel']);
            mcu_tft_fb_draw_text(84, 170, $curr['updated'], $c['fg'], 1, 1);
        }
        if ($force_all || $tft_prev['ip4'] !== $curr['ip4']) {
            mcu_tft_fb_fill_rect(44, 188, MCU_TFT_WIDTH - 52, 12, $c['panel']);
            mcu_tft_fb_draw_text(44, 188, $curr['ip4'], $c['fg'], 1, 1);
        }
        if ($force_all || $tft_prev['ip6'] !== $curr['ip6']) {
            mcu_tft_fb_fill_rect(44, 204, MCU_TFT_WIDTH - 52, 12, $c['panel']);
            mcu_tft_fb_draw_text(44, 204, $curr['ip6'], $c['fg'], 1, 1);
        }
        $t_text = microtime(true);
        $tft_prev = $curr;

        print "tft:render:text\n";
        mcu_tft_fb_render();
        $t_blit = microtime(true);
        print "render_ms:";
        print (int) (($t_blit - $render_t0) * 1000.0);
        print " base_ms:";
        print (int) (($t_base - $t0) * 1000.0);
        print " text_ms:";
        print (int) (($t_text - $t_base) * 1000.0);
        print " blit_ms:";
        print (int) (($t_blit - $t_text) * 1000.0);
        print "\n";
    }
}

$mode_logo = false;
$prev_mask = 0;
$needs_redraw = false;
$next_log_s = time() + 1;
$led_mask = 0;
$raw_vbat = mcu_battery_raw_vbat();
$raw_vref = mcu_battery_raw_vref();
$batt_v = mcu_battery_voltage_from_raw($raw_vbat, $raw_vref);
$usb_connected = mcu_usb_connected();
$batt_pct = mcu_battery_level_from_voltage($batt_v);
$prev_batt_pct = $batt_pct;
$charging = mcu_is_charging_estimate($batt_v, $usb_connected);
$wifi_status = MCU_WIFI_LINK_DOWN;
$wifi_ip4 = null;
$wifi_ip6 = null;
$light_raw = function_exists('mcu_light_raw') ? mcu_light_raw() : 0;
$backlight_pct = 100;
if (function_exists('mcu_tft_backlight')) {
    mcu_tft_backlight(65535);
}
if (function_exists('mcu_tft_clear')) {
    mcu_tft_clear(mcu_rgb565(0, 0, 0));
}

run_pcre_smoke();
run_hash_smoke();
run_json_smoke();

/* Initial render immediately, before Wi-Fi/bootstrap work. */
redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6, $light_raw, $backlight_pct);

$wifi_ssid = getenv('WIFI_SSID');
$wifi_pass = getenv('WIFI_PASS');
if (is_string($wifi_ssid) && $wifi_ssid !== '') {
    print "wifi:init\n";
    $wifi_ok = mcu_wifi_connect($wifi_ssid, is_string($wifi_pass) ? $wifi_pass : null, 15000);
    print "wifi:ok:";
    print $wifi_ok ? "1" : "0";
    print " status:";
    $wifi_status = mcu_wifi_status();
    print $wifi_status;
    $wifi_ip4 = mcu_wifi_ip4();
    $wifi_ip6 = mcu_wifi_ip6();
    print " ip4:";
    print is_string($wifi_ip4) ? $wifi_ip4 : "none";
    print " ip6:";
    print is_string($wifi_ip6) ? $wifi_ip6 : "none";
    print "\n";

    if ($wifi_ok && $wifi_status === MCU_WIFI_LINK_UP) {
        $ntp_ok = mcu_ntp_sync('pool.ntp.org', 10000);
        print "ntp:ok:";
        print $ntp_ok ? "1" : "0";
        print " now:";
        print time();
        print "\n";

        $http = file_get_contents('http://example.com/');
        if ($http === false) {
            print "http:wrapper:fail\n";
        } else {
            print "http:wrapper:ok len:";
            print strlen($http);
            print " head:";
            print substr($http, 0, 24);
            print "\n";
        }

        $https = file_get_contents('https://example.com/');
        if ($https === false) {
            print "https:wrapper:fail\n";
        } else {
            print "https:wrapper:ok len:";
            print strlen($https);
            print " head:";
            print substr($https, 0, 24);
            print "\n";
        }

        $ctx = stream_context_create([
            'http' => [
                'method' => 'GET',
                'timeout' => 6.0,
                'header' => "X-RP2350-Context: 1\r\n",
            ],
        ]);
        $https_ctx = file_get_contents('https://example.com/', false, $ctx);
        if ($https_ctx === false) {
            print "https:ctx:fail\n";
        } else {
            print "https:ctx:ok len:";
            print strlen($https_ctx);
            print " head:";
            print substr($https_ctx, 0, 24);
            print "\n";
        }

    }
}

/* Re-baseline after boot work (wifi/ntp/http can take time and shift state). */
$raw_vbat = mcu_battery_raw_vbat();
$raw_vref = mcu_battery_raw_vref();
$batt_v = mcu_battery_voltage_from_raw($raw_vbat, $raw_vref);
$usb_connected = mcu_usb_connected();
$batt_pct = mcu_battery_level_from_voltage($batt_v);
$charging = mcu_is_charging_estimate($batt_v, $usb_connected);
$wifi_status = mcu_wifi_status();
$wifi_ip4 = mcu_wifi_ip4();
$wifi_ip6 = mcu_wifi_ip6();
$light_raw = function_exists('mcu_light_raw') ? mcu_light_raw() : 0;

$prev_batt_pct = $batt_pct;
$prev_usb_connected = $usb_connected;
$prev_charging = $charging;
$prev_wifi_status = $wifi_status;
$prev_wifi_ip4 = $wifi_ip4;
$prev_wifi_ip6 = $wifi_ip6;
$next_log_s = time() + 1;

while (true) {
    $now_s = time();
    $remaining_ms = ($next_log_s - $now_s) * 1000;
    if ($remaining_ms <= 0) {
        $remaining_ms = 1;
    }
    if ($remaining_ms > 50) {
        $remaining_ms = 50;
    }

    /* LED policy: A/UP/DOWN mirror buttons; LED1 breathes while charging. */
    mcu_led_set(MCU_LED_0, (($led_mask & (1 << MCU_BTN_A)) !== 0));
    mcu_led_set(MCU_LED_2, (($led_mask & (1 << MCU_BTN_UP)) !== 0));
    mcu_led_set(MCU_LED_3, (($led_mask & (1 << MCU_BTN_DOWN)) !== 0));
    if ($charging) {
        $period_ms = 1800;
        $half = (int) ($period_ms / 2);
        $t = (int) fmod(microtime(true) * 1000.0, (float)$period_ms);
        $tri = ($t < $half) ? $t : ($period_ms - $t);
        $linear = (int) (($tri * 65535) / $half);
        $gamma = (int) (($linear * $linear) / 65535);
        mcu_led_level(MCU_LED_1, $gamma);
    } else {
        mcu_led_set(MCU_LED_1, (($led_mask & (1 << MCU_BTN_B)) !== 0));
    }

    $mask = mcu_button_wait($remaining_ms);
    if ($mask !== false) {
        $c_down = (($mask & (1 << MCU_BTN_C)) !== 0);
        $c_was_down = (($prev_mask & (1 << MCU_BTN_C)) !== 0);
        $up_down = (($mask & (1 << MCU_BTN_UP)) !== 0);
        $up_was_down = (($prev_mask & (1 << MCU_BTN_UP)) !== 0);
        $down_down = (($mask & (1 << MCU_BTN_DOWN)) !== 0);
        $down_was_down = (($prev_mask & (1 << MCU_BTN_DOWN)) !== 0);
        if ($up_down && !$up_was_down && function_exists('mcu_tft_backlight')) {
            $backlight_pct += 10;
            if ($backlight_pct > 100) {
                $backlight_pct = 100;
            }
            mcu_tft_backlight((int)(($backlight_pct * 65535) / 100));
            print "backlight:";
            print $backlight_pct;
            print "\n";
        }
        if ($down_down && !$down_was_down && function_exists('mcu_tft_backlight')) {
            $backlight_pct -= 10;
            if ($backlight_pct < 0) {
                $backlight_pct = 0;
            }
            mcu_tft_backlight((int)(($backlight_pct * 65535) / 100));
            print "backlight:";
            print $backlight_pct;
            print "\n";
        }
        if ($c_down && !$c_was_down) {
            $mode_logo = !$mode_logo;
            $needs_redraw = true;
            print "mode:";
            print $mode_logo ? "logo\n" : "text\n";
        }
        $prev_mask = $mask;
        $led_mask = $mask;
        if ($needs_redraw) {
            redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6, $light_raw, $backlight_pct);
            $needs_redraw = false;
        }
        continue;
    }

    $now_s = time();
    if ($now_s < $next_log_s) {
        continue;
    }
    $next_log_s = $now_s + 1;

    $raw_vbat = mcu_battery_raw_vbat();
    $raw_vref = mcu_battery_raw_vref();
    $batt_v = mcu_battery_voltage_from_raw($raw_vbat, $raw_vref);
    $usb_connected = mcu_usb_connected();
    $batt_pct = mcu_battery_level_from_voltage($batt_v);
    $charging = mcu_is_charging_estimate($batt_v, $usb_connected);
    $wifi_status = mcu_wifi_status();
    $wifi_ip4 = mcu_wifi_ip4();
    $wifi_ip6 = mcu_wifi_ip6();
    $light_raw = function_exists('mcu_light_raw') ? mcu_light_raw() : 0;
    if ($batt_pct !== $prev_batt_pct
        || $usb_connected !== $prev_usb_connected
        || $charging !== $prev_charging
        || $wifi_status !== $prev_wifi_status
        || $wifi_ip4 !== $prev_wifi_ip4
        || $wifi_ip6 !== $prev_wifi_ip6) {
        $needs_redraw = true;
        $prev_batt_pct = $batt_pct;
        $prev_usb_connected = $usb_connected;
        $prev_charging = $charging;
        $prev_wifi_status = $wifi_status;
        $prev_wifi_ip4 = $wifi_ip4;
        $prev_wifi_ip6 = $wifi_ip6;
    }

    if ($needs_redraw) {
        redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6, $light_raw, $backlight_pct);
        $needs_redraw = false;
    }

    print "time:";
    print time();
    print " microtime_s:";
    print microtime();
    print " microtime_f:";
    print microtime(true);
    $hrt = hrtime();
    print " hrtime_a:";
    print $hrt[0];
    print ".";
    print $hrt[1];
    print " hrtime_n:";
    print hrtime(true);
    print " batt_v:";
    print $batt_v;
    print " batt_pct:";
    print $batt_pct;
    print " raw_vbat:";
    print $raw_vbat;
    print " raw_vref:";
    print $raw_vref;
    print " light_raw:";
    print $light_raw;
    print " bl:";
    print $backlight_pct;
    print " usb:";
    print $usb_connected ? "1" : "0";
    print " chg:";
    print $charging ? "1" : "0";
    print " wifi:";
    print wifi_status_label($wifi_status);
    print " ip4:";
    print is_string($wifi_ip4) ? $wifi_ip4 : "none";
    print " ip6:";
    print is_string($wifi_ip6) ? $wifi_ip6 : "none";
    print "\n";
}
