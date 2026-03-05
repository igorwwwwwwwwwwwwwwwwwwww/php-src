<?php
require '/lib.php';
require '/logo.php';

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

function redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6) {
    if ($mode_logo) {
        $logo = php_logo_data();
        if ($logo !== false) {
            $buf = $logo[0];
            $w = $logo[1];
            $h = $logo[2];
            print "epd:render:logo\n";
            mcu_epd_render($buf, $w, $h);
        }
        return;
    }

    $buf = mcu_fb_create(MCU_EPD_WIDTH, MCU_EPD_HEIGHT, false);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 18, 18, 'PHP RP2350', 3, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 56, 'BUTTONS -> LEDS', 2, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 80, 'BAT', 2, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 74, 80, (string)$batt_pct, 2, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 118, 80, $usb_connected ? 'USB' : 'BAT', 2, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 168, 80, $charging ? 'CHG' : 'IDLE', 2, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 104, 'WIFI', 2, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 74, 104, wifi_status_label($wifi_status), 2, 2);
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 124, 'V4', 1, 1);
    mcu_draw_text(
        $buf,
        MCU_EPD_WIDTH,
        MCU_EPD_HEIGHT,
        44,
        124,
        (is_string($wifi_ip4) && $wifi_ip4 !== '') ? $wifi_ip4 : '-',
        1,
        1
    );
    mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 138, 'V6', 1, 1);
    mcu_draw_text(
        $buf,
        MCU_EPD_WIDTH,
        MCU_EPD_HEIGHT,
        44,
        138,
        (is_string($wifi_ip6) && $wifi_ip6 !== '') ? $wifi_ip6 : '-',
        1,
        1
    );
    print "epd:render:text\n";
    mcu_epd_render($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT);
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

/* Initial render exactly once at boot. */
redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6);

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
        if ($c_down && !$c_was_down) {
            $mode_logo = !$mode_logo;
            $needs_redraw = true;
            print "mode:";
            print $mode_logo ? "logo\n" : "text\n";
        }
        $prev_mask = $mask;
        $led_mask = $mask;
        if ($needs_redraw) {
            redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6);
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
        redraw_mode($mode_logo, $batt_pct, $usb_connected, $charging, $wifi_status, $wifi_ip4, $wifi_ip6);
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
