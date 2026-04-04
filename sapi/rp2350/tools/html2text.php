<?php
require __DIR__ . '/html2text_lib.php';

if ($argc < 2) {
    fwrite(STDERR, "usage: php sapi/rp2350/tools/html2text.php <url-or-file> [width] [max-lines]\n");
    exit(1);
}

$input = $argv[1];
$width = isset($argv[2]) ? max(10, (int)$argv[2]) : 72;
$maxLines = isset($argv[3]) ? max(1, (int)$argv[3]) : 120;

try {
    $html = html2text_fetch_input($input);
    $lines = html2text_render_lines($html, $width, $maxLines, preg_match('~^https?://~i', $input) ? $input : null);
    foreach ($lines as $line) {
        echo $line, "\n";
    }
} catch (Throwable $e) {
    fwrite(STDERR, $e->getMessage() . "\n");
    exit(2);
}
