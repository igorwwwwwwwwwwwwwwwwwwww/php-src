<?php

if ($argc < 3) {
    fwrite(STDERR, "usage: php sapi/rp2350/tools/html2text_render.php <url-or-file> <out.png> [width] [height] [max-lines]\n");
    exit(1);
}

$input = $argv[1];
$out = $argv[2];
$width = isset($argv[3]) ? max(64, (int)$argv[3]) : 320;
$height = isset($argv[4]) ? max(64, (int)$argv[4]) : 240;
$maxLines = isset($argv[5]) ? max(1, (int)$argv[5]) : 14;

if (!extension_loaded('gd')) {
    fwrite(STDERR, "php-gd is required\n");
    exit(2);
}

require __DIR__ . '/html2text_lib.php';

function html2text_rasterize_svg_to_png(string $svgPath, string $pngPath, int $width, int $height): bool {
    $localCmd = 'php ' . escapeshellarg(__DIR__ . '/svg_logo_raster.php') . ' ' . escapeshellarg($svgPath) . ' ' . escapeshellarg($pngPath) . ' ' . (int)$width . ' ' . (int)$height;
    exec($localCmd, $out, $rc);
    if ($rc === 0 && is_file($pngPath) && filesize($pngPath) > 0) {
        return true;
    }
    $cmd = 'rsvg-convert -w ' . (int)$width . ' -h ' . (int)$height . ' ' . escapeshellarg($svgPath) . ' -o ' . escapeshellarg($pngPath);
    exec($cmd, $out, $rc);
    return $rc === 0 && is_file($pngPath) && filesize($pngPath) > 0;
}

function html2text_fetch_image_file(string $url): ?string {
    try {
        $body = html2text_fetch_url($url);
    } catch (Throwable $e) {
        return null;
    }
    $ext = pathinfo(parse_url($url, PHP_URL_PATH) ?? '', PATHINFO_EXTENSION);
    if ($ext === '') {
        $ext = 'bin';
    }
    $tmp = tempnam(sys_get_temp_dir(), 'h2timg_');
    if ($tmp === false) {
        return null;
    }
    $path = $tmp . '.' . $ext;
    rename($tmp, $path);
    file_put_contents($path, $body);
    return $path;
}

$html = html2text_fetch_input($input);
$baseUrl = preg_match('~^https?://~i', $input) ? $input : null;
$lines = html2text_render_lines($html, 34, $maxLines, $baseUrl);
$bgImageInfo = html2text_find_background_image_info($html, $baseUrl);
$imageInfo = html2text_find_first_image_info($html, $baseUrl);

$im = imagecreatetruecolor($width, $height);
imagealphablending($im, true);
imagesavealpha($im, true);

$bg = imagecolorallocate($im, 7, 12, 20);
$panel = imagecolorallocate($im, 18, 28, 40);
$accent = imagecolorallocate($im, 0, 180, 220);
$accent2 = imagecolorallocate($im, 255, 180, 0);
$fg = imagecolorallocate($im, 245, 248, 250);
$muted = imagecolorallocate($im, 150, 168, 182);

imagefilledrectangle($im, 0, 0, $width - 1, $height - 1, $bg);
imagefilledrectangle($im, 8, 8, $width - 9, $height - 9, $panel);

if (is_array($bgImageInfo) && !empty($bgImageInfo['url'])) {
    $imgPath = html2text_fetch_image_file($bgImageInfo['url']);
    if ($imgPath) {
        $renderPath = $imgPath;
        if (preg_match('~\.svg$~i', $imgPath)) {
            $pngPath = tempnam(sys_get_temp_dir(), 'h2tbgsvg_');
            if ($pngPath !== false) {
                unlink($pngPath);
                $pngPath .= '.png';
                if (html2text_rasterize_svg_to_png($imgPath, $pngPath, $width - 16, $height - 16)) {
                    $renderPath = $pngPath;
                }
            }
        }
        $bgImg = @imagecreatefromstring((string)@file_get_contents($renderPath));
        if ($bgImg !== false) {
            $bw = imagesx($bgImg);
            $bh = imagesy($bgImg);
            if ($bw > 0 && $bh > 0) {
                for ($ty = 8; $ty < $height - 8; $ty += $bh) {
                    for ($tx = 8; $tx < $width - 8; $tx += $bw) {
                        imagecopy($im, $bgImg, $tx, $ty, 0, 0, min($bw, $width - 8 - $tx), min($bh, $height - 8 - $ty));
                    }
                }
            }
        }
    }
}

$y = 14;
if (is_array($imageInfo) && !empty($imageInfo['url'])) {
    $imgPath = html2text_fetch_image_file($imageInfo['url']);
    if ($imgPath) {
        $renderPath = $imgPath;
        if (preg_match('~\.svg$~i', $imgPath)) {
            $pngPath = tempnam(sys_get_temp_dir(), 'h2tsvg_');
            if ($pngPath !== false) {
                unlink($pngPath);
                $pngPath .= '.png';
                if (html2text_rasterize_svg_to_png($imgPath, $pngPath, 180, 72)) {
                    $renderPath = $pngPath;
                }
            }
        }
        $img = @imagecreatefromstring((string)@file_get_contents($renderPath));
        if ($img !== false) {
            $iw = imagesx($img);
            $ih = imagesy($img);
            if ($iw > 0 && $ih > 0) {
                $maxW = 180;
                $maxH = 72;
                $scale = min($maxW / $iw, $maxH / $ih, 1.0);
                $dw = max(1, (int)round($iw * $scale));
                $dh = max(1, (int)round($ih * $scale));
                $dx = (int)(($width - $dw) / 2);
                $dy = 14;
                imagecopyresampled($im, $img, $dx, $dy, 0, 0, $dw, $dh, $iw, $ih);
                $y = $dy + $dh + 12;
            }
        }
    }
}
$lineHeight = 12;
foreach ($lines as $line) {
    imagestring($im, 3, 14, $y, $line, $fg);
    $y += $lineHeight;
    if ($y > $height - 20) {
        break;
    }
}

imagepng($im, $out);
echo "wrote $out\n";
