<?php

const MCU_EPD_WIDTH = 264;
const MCU_EPD_HEIGHT = 176;
const MCU_TFT_WIDTH = 320;
const MCU_TFT_HEIGHT = 240;
const MCU_BAT_MAX_V = 4.10;

const MCU_FONT_5X7 = array(
    ' ' => array(0, 0, 0, 0, 0, 0, 0),
    '.' => array(0, 0, 0, 0, 0, 0x06, 0x06),
    ':' => array(0, 0x06, 0x06, 0, 0x06, 0x06, 0),
    '-' => array(0, 0, 0, 0x1e, 0, 0, 0),
    '+' => array(0, 0x04, 0x04, 0x1f, 0x04, 0x04, 0),
    '<' => array(0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02),
    '>' => array(0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08),
    '_' => array(0, 0, 0, 0, 0, 0, 0x1f),
    '/' => array(0x01, 0x02, 0x04, 0x08, 0x10, 0, 0),
    '0' => array(0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e),
    '1' => array(0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e),
    '2' => array(0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f),
    '3' => array(0x1f, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0e),
    '4' => array(0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02),
    '5' => array(0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e),
    '6' => array(0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e),
    '7' => array(0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08),
    '8' => array(0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e),
    '9' => array(0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c),
    'A' => array(0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11),
    'B' => array(0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e),
    'C' => array(0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e),
    'D' => array(0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c),
    'E' => array(0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f),
    'F' => array(0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10),
    'G' => array(0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f),
    'H' => array(0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11),
    'I' => array(0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e),
    'J' => array(0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e),
    'K' => array(0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11),
    'L' => array(0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f),
    'M' => array(0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11),
    'N' => array(0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11),
    'O' => array(0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e),
    'P' => array(0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10),
    'Q' => array(0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d),
    'R' => array(0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11),
    'S' => array(0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e),
    'T' => array(0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    'U' => array(0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e),
    'V' => array(0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04),
    'W' => array(0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a),
    'X' => array(0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11),
    'Y' => array(0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04),
    'Z' => array(0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f),
);

function mcu_fb_create($w, $h, $black = false) {
    $stride = ($w + 7) >> 3;
    $fill = $black ? "\xff" : "\x00";
    return str_repeat($fill, $stride * $h);
}

function mcu_fb_set_pixel(&$buf, $w, $h, $x, $y, $black) {
    if ($x < 0 || $y < 0 || $x >= $w || $y >= $h) {
        return;
    }
    $stride = ($w + 7) >> 3;
    $idx = ($y * $stride) + ($x >> 3);
    $mask = 1 << (7 - ($x & 7));
    $v = ord($buf[$idx]);
    if ($black) {
        $v |= $mask;
    } else {
        $v &= (~$mask) & 0xff;
    }
    $buf[$idx] = chr($v);
}

function mcu_draw_char(&$buf, $w, $h, $x, $y, $ch, $scale = 1) {
    if ($scale < 1) {
        $scale = 1;
    }
    if (!isset(MCU_FONT_5X7[$ch])) {
        $ch = ' ';
    }
    $rows = MCU_FONT_5X7[$ch];
    for ($row = 0; $row < 7; $row++) {
        $bits = $rows[$row];
        for ($col = 0; $col < 5; $col++) {
            if (($bits & (1 << (4 - $col))) === 0) {
                continue;
            }
            for ($sy = 0; $sy < $scale; $sy++) {
                for ($sx = 0; $sx < $scale; $sx++) {
                    mcu_fb_set_pixel($buf, $w, $h, $x + ($col * $scale) + $sx, $y + ($row * $scale) + $sy, true);
                }
            }
        }
    }
}

function mcu_draw_text(&$buf, $w, $h, $x, $y, $text, $scale = 1, $spacing = 1) {
    $cx = $x;
    $len = strlen($text);
    for ($i = 0; $i < $len; $i++) {
        $ch = $text[$i];
        if ($ch >= 'a' && $ch <= 'z') {
            $ch = chr(ord($ch) - 32);
        }
        mcu_draw_char($buf, $w, $h, $cx, $y, $ch, $scale);
        $cx += (5 * $scale) + $spacing;
    }
}

function mcu_battery_level_from_voltage($voltage) {
    if (!is_numeric($voltage)) {
        return 0;
    }
    $v = (float)$voltage;
    if ($v <= 0.0 || $v > 6.0) {
        return 0;
    }

    $base = 1.0 + pow(($v / 3.2), 80.0);
    $den = pow($base, 0.165);
    if (!is_numeric($den) || $den <= 0.0) {
        return 0;
    }

    $pct = round(123.0 - (123.0 / $den));
    if ($pct < 0) {
        return 0;
    }
    if ($pct > 100) {
        return 100;
    }
    return $pct;
}

function mcu_battery_level() {
    return mcu_battery_level_from_voltage(mcu_battery_voltage());
}

function mcu_is_charging_estimate($voltage, $usb_connected) {
    return $usb_connected && ($voltage < MCU_BAT_MAX_V);
}

function mcu_battery_voltage_from_raw($raw_vbat, $raw_vref) {
    if (!is_numeric($raw_vbat) || !is_numeric($raw_vref)) {
        return 0.0;
    }
    $vbat = (int)($raw_vbat + 0);
    $vref = (int)($raw_vref + 0);
    if ($vbat <= 0 || $vref <= 0) {
        return 0.0;
    }
    /* Mirror stock path with integer math first: volts = (vbat/vref) * 2.2 */
    $lhs = ($vbat * 2200);
    $half = ($vref / 2.0);
    $num = ($lhs + $half);
    $mv = (int) floor($num / $vref);
    if ($mv <= 0 || $mv > 6000) {
        return 0.0;
    }
    return $mv / 1000.0;
}

function mcu_rgb565($r, $g, $b) {
    $r = (int)$r;
    $g = (int)$g;
    $b = (int)$b;
    if ($r < 0) $r = 0;
    if ($r > 255) $r = 255;
    if ($g < 0) $g = 0;
    if ($g > 255) $g = 255;
    if ($b < 0) $b = 0;
    if ($b > 255) $b = 255;
    return (($r & 0xf8) << 8) | (($g & 0xfc) << 3) | ($b >> 3);
}

function mcu_tft_fb_create($w, $h, $color = 0x0000) {
    $px = pack('n', $color & 0xffff);
    return str_repeat($px, $w * $h);
}

function mcu_tft_set_pixel_rgb565(&$buf, $w, $h, $x, $y, $color) {
    if ($x < 0 || $y < 0 || $x >= $w || $y >= $h) {
        return;
    }
    $idx = (($y * $w) + $x) * 2;
    $color &= 0xffff;
    $buf[$idx] = chr(($color >> 8) & 0xff);
    $buf[$idx + 1] = chr($color & 0xff);
}

function mcu_tft_fill_rect(&$buf, $w, $h, $x, $y, $rw, $rh, $color) {
    if ($rw <= 0 || $rh <= 0) {
        return;
    }
    for ($yy = 0; $yy < $rh; $yy++) {
        for ($xx = 0; $xx < $rw; $xx++) {
            mcu_tft_set_pixel_rgb565($buf, $w, $h, $x + $xx, $y + $yy, $color);
        }
    }
}

function mcu_tft_draw_char(&$buf, $w, $h, $x, $y, $ch, $fg, $bg = null, $scale = 1) {
    if ($scale < 1) {
        $scale = 1;
    }
    if (!isset(MCU_FONT_5X7[$ch])) {
        $ch = ' ';
    }
    $rows = MCU_FONT_5X7[$ch];
    for ($row = 0; $row < 7; $row++) {
        $bits = $rows[$row];
        for ($col = 0; $col < 5; $col++) {
            $on = (($bits & (1 << (4 - $col))) !== 0);
            if (!$on && $bg === null) {
                continue;
            }
            $color = $on ? $fg : $bg;
            for ($sy = 0; $sy < $scale; $sy++) {
                for ($sx = 0; $sx < $scale; $sx++) {
                    mcu_tft_set_pixel_rgb565($buf, $w, $h, $x + ($col * $scale) + $sx, $y + ($row * $scale) + $sy, $color);
                }
            }
        }
    }
}

function mcu_tft_draw_text(&$buf, $w, $h, $x, $y, $text, $fg, $bg = null, $scale = 1, $spacing = 1) {
    $cx = $x;
    $len = strlen($text);
    for ($i = 0; $i < $len; $i++) {
        $ch = $text[$i];
        if ($ch >= 'a' && $ch <= 'z') {
            $ch = chr(ord($ch) - 32);
        }
        mcu_tft_draw_char($buf, $w, $h, $cx, $y, $ch, $fg, $bg, $scale);
        $cx += (5 * $scale) + $spacing;
    }
}
