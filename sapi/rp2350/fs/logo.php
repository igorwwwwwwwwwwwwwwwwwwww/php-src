<?php
const PHP_LOGO_W = 121;
const PHP_LOGO_H = 64;
const PHP_LOGO_PATH = '/php_logo_1bpp.bin';

function php_logo_bytes(): string {
    $d = file_get_contents(PHP_LOGO_PATH);
    return $d === false ? '' : $d;
}
