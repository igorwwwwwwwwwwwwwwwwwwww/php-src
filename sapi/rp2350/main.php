<?php
require '/lib.php';
print "req:lib:ok\n";
require '/logo.php';
print "req:logo:ok\n";

$mode_logo = false;
$prev_mask = 0;
$needs_redraw = true;
$next_log_s = time() + 1;
$led_mask = 0;

while (true) {
    $now_s = time();
    $remaining_ms = ($next_log_s - $now_s) * 1000;
    if ($remaining_ms <= 0) {
        $remaining_ms = 1;
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
        mcu_led_set(MCU_LED_0, (($led_mask & (1 << MCU_BTN_A)) !== 0));
        mcu_led_set(MCU_LED_1, (($led_mask & (1 << MCU_BTN_B)) !== 0));
        mcu_led_set(MCU_LED_2, (($led_mask & (1 << MCU_BTN_UP)) !== 0));
        mcu_led_set(MCU_LED_3, (($led_mask & (1 << MCU_BTN_DOWN)) !== 0));
        if ($needs_redraw) {
            if ($mode_logo) {
                print "logo:decode:start\n";
                $logo = php_logo_data();
                print "logo:decode:done\n";
                if ($logo !== false) {
                    $buf = $logo[0];
                    $w = $logo[1];
                    $h = $logo[2];
                    mcu_epd_render($buf, $w, $h);
                    print "logo:render:done\n";
                } else {
                    print "logo:decode:fail\n";
                }
            } else {
                print "text:render:start\n";
                $buf = mcu_fb_create(MCU_EPD_WIDTH, MCU_EPD_HEIGHT, false);
                mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 18, 18, 'PHP RP2350', 3, 2);
                mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 56, 'BUTTONS -> LEDS', 2, 2);
                mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 80, 'TIME + UART LOOP', 2, 2);
                mcu_epd_render($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT);
                print "text:render:done\n";
            }
            $needs_redraw = false;
        }
        continue;
    }

    $now_s = time();
    if ($now_s < $next_log_s) {
        continue;
    }
    $next_log_s = $now_s + 1;

    if ($needs_redraw) {
        if ($mode_logo) {
            print "logo:decode:start\n";
            $logo = php_logo_data();
            print "logo:decode:done\n";
            if ($logo !== false) {
                $buf = $logo[0];
                $w = $logo[1];
                $h = $logo[2];
                mcu_epd_render($buf, $w, $h);
                print "logo:render:done\n";
            } else {
                print "logo:decode:fail\n";
            }
        } else {
            print "text:render:start\n";
            $buf = mcu_fb_create(MCU_EPD_WIDTH, MCU_EPD_HEIGHT, false);
            mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 18, 18, 'PHP RP2350', 3, 2);
            mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 56, 'BUTTONS -> LEDS', 2, 2);
            mcu_draw_text($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT, 20, 80, 'TIME + UART LOOP', 2, 2);
            mcu_epd_render($buf, MCU_EPD_WIDTH, MCU_EPD_HEIGHT);
            print "text:render:done\n";
        }
        $needs_redraw = false;
    }

    $fp = fopen('/lib.php', 'rb');
    if ($fp === false) {
        print "fopen-fail\n";
    } else {
        $probe = fread($fp, 1);
        $ch = fgetc($fp);
        fclose($fp);
        if ($probe === false) {
            print "fread-fail\n";
        } else if ($probe === '') {
            print "fread-empty\n";
        } else {
            print "fread-ok:";
            print ord($probe);
            print "\n";
        }
        if ($ch === false) {
            print "fgetc-fail\n";
        } else {
            print "fgetc-ok:";
            print ord($ch);
            print "\n";
        }
    }
    $lib = file_get_contents('/lib.php');
    if ($lib === false) {
        print "fgc-fail\n";
    } else {
        print "fgc-ok:";
        print strlen($lib);
        print "\n";
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
    print "\n";
    print tick_line();
}
