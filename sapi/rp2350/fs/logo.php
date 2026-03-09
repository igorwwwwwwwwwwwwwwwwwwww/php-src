<?php
const PHP_LOGO_BMP_PATH = '/php_logo.bmp';
const PHP_LOGO_RGB565_PATH = '/php_logo.rgb565';
const PHP_LOGO_RGB565_META_PATH = '/php_logo.rgb565.json';

function _u16le($s, $o) {
    return ord($s[$o]) | (ord($s[$o + 1]) << 8);
}

function _u32le($s, $o) {
    return ord($s[$o]) | (ord($s[$o + 1]) << 8) | (ord($s[$o + 2]) << 16) | (ord($s[$o + 3]) << 24);
}

function php_logo_data() {
    $bmp = file_get_contents(PHP_LOGO_BMP_PATH);
    if ($bmp === false) {
        return false;
    }
    $bmp_len = strlen($bmp);
    if ($bmp_len < 54) {
        return false;
    }
    if (ord($bmp[0]) !== 0x42 || ord($bmp[1]) !== 0x4d) {
        return false;
    }

    $pixel_offset = _u32le($bmp, 10);
    $dib_size = _u32le($bmp, 14);
    if ($dib_size < 40 || $bmp_len < $pixel_offset) {
        return false;
    }

    $width = _u32le($bmp, 18);
    $height = _u32le($bmp, 22);
    $bpp = _u16le($bmp, 28);
    $compression = _u32le($bmp, 30);

    if ($width <= 0 || $height <= 0 || $bpp !== 24 || $compression !== 0) {
        return false;
    }

    $row_stride = ((($width * 3) + 3) >> 2) << 2;
    $out_stride = ($width + 7) >> 3;
    $need = $pixel_offset + ($row_stride * $height);
    if ($bmp_len < $need) {
        return false;
    }

    $out = str_repeat("\x00", $out_stride * $height);
    for ($y = 0; $y < $height; $y++) {
        $src_y = $height - 1 - $y;
        $row_base = $pixel_offset + ($src_y * $row_stride);
        $dst_base = $y * $out_stride;
        for ($x = 0; $x < $width; $x++) {
            $p = $row_base + ($x * 3);
            $b = ord($bmp[$p + 0]);
            $g = ord($bmp[$p + 1]);
            $r = ord($bmp[$p + 2]);
            $luma = (($r * 54) + ($g * 183) + ($b * 19)) >> 8;
            if ($luma < 170) {
                $idx = $dst_base + ($x >> 3);
                $mask = 1 << (7 - ($x & 7));
                $out[$idx] = chr(ord($out[$idx]) | $mask);
            }
        }
    }

    return array($out, $width, $height);
}

function php_logo_rgb565_data() {
    $meta_json = file_get_contents(PHP_LOGO_RGB565_META_PATH);
    if ($meta_json === false) {
        return false;
    }
    $meta = json_decode($meta_json, true);
    if (!is_array($meta)) {
        return false;
    }
    $w = isset($meta['width']) ? (int)$meta['width'] : 0;
    $h = isset($meta['height']) ? (int)$meta['height'] : 0;
    if ($w <= 0 || $h <= 0) {
        return false;
    }
    $buf = file_get_contents(PHP_LOGO_RGB565_PATH);
    if ($buf === false) {
        return false;
    }
    if (strlen($buf) < ($w * $h * 2)) {
        return false;
    }
    return array($buf, $w, $h);
}
