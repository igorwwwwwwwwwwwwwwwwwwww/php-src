<?php

if ($argc < 3) {
    fwrite(STDERR, "usage: php sapi/rp2350/tools/svg_logo_raster.php <in.svg> <out.png> [width] [height]\n");
    exit(1);
}

$in = $argv[1];
$out = $argv[2];
$targetW = isset($argv[3]) ? max(16, (int)$argv[3]) : 320;
$targetH = isset($argv[4]) ? max(16, (int)$argv[4]) : 240;

if (!extension_loaded('gd')) {
    fwrite(STDERR, "php-gd is required\n");
    exit(2);
}

$svg = file_get_contents($in);
if ($svg === false) {
    fwrite(STDERR, "read failed: $in\n");
    exit(2);
}

if (!preg_match('~viewBox="([^"]+)"~i', $svg, $m)) {
    fwrite(STDERR, "missing viewBox\n");
    exit(3);
}
$vb = preg_split('~[ ,]+~', trim($m[1]));
if (count($vb) !== 4) {
    fwrite(STDERR, "bad viewBox\n");
    exit(3);
}
[$minX, $minY, $vbW, $vbH] = array_map('floatval', $vb);

$fillHex = '#ffffff';
if (preg_match('~<svg[^>]*\sfill="([^"]+)"~i', $svg, $m)) {
    $fillHex = trim($m[1]);
}

preg_match_all('~<path\b[^>]*\sd="([^"]+)"[^>]*>~i', $svg, $matches);
$paths = $matches[1] ?? [];
if (!$paths) {
    fwrite(STDERR, "no paths\n");
    exit(4);
}

function svg_tokenize_path(string $d): array {
    preg_match_all('~[a-zA-Z]|[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?~', $d, $m);
    return $m[0];
}

function cubic_point(float $x0, float $y0, float $x1, float $y1, float $x2, float $y2, float $x3, float $y3, float $t): array {
    $mt = 1.0 - $t;
    $mt2 = $mt * $mt;
    $mt3 = $mt2 * $mt;
    $t2 = $t * $t;
    $t3 = $t2 * $t;
    $x = $mt3 * $x0 + 3 * $mt2 * $t * $x1 + 3 * $mt * $t2 * $x2 + $t3 * $x3;
    $y = $mt3 * $y0 + 3 * $mt2 * $t * $y1 + 3 * $mt * $t2 * $y2 + $t3 * $y3;
    return [$x, $y];
}

function svg_parse_path_to_polylines(string $d): array {
    $tokens = svg_tokenize_path($d);
    $i = 0;
    $cmd = null;
    $x = 0.0;
    $y = 0.0;
    $subpaths = [];
    $current = [];

    while ($i < count($tokens)) {
        if (preg_match('~^[a-zA-Z]$~', $tokens[$i])) {
            $cmd = $tokens[$i++];
        }
        if ($cmd === null) {
            break;
        }
        switch ($cmd) {
            case 'm': {
                if ($i + 1 >= count($tokens)) break 2;
                $x += (float)$tokens[$i++];
                $y += (float)$tokens[$i++];
                if ($current) {
                    $subpaths[] = $current;
                }
                $current = [[$x, $y]];
                while ($i + 1 < count($tokens) && !preg_match('~^[a-zA-Z]$~', $tokens[$i])) {
                    $x += (float)$tokens[$i++];
                    $y += (float)$tokens[$i++];
                    $current[] = [$x, $y];
                }
                break;
            }
            case 'l': {
                while ($i + 1 < count($tokens) && !preg_match('~^[a-zA-Z]$~', $tokens[$i])) {
                    $x += (float)$tokens[$i++];
                    $y += (float)$tokens[$i++];
                    $current[] = [$x, $y];
                }
                break;
            }
            case 'c': {
                while ($i + 5 < count($tokens) && !preg_match('~^[a-zA-Z]$~', $tokens[$i])) {
                    $x1 = $x + (float)$tokens[$i++];
                    $y1 = $y + (float)$tokens[$i++];
                    $x2 = $x + (float)$tokens[$i++];
                    $y2 = $y + (float)$tokens[$i++];
                    $x3 = $x + (float)$tokens[$i++];
                    $y3 = $y + (float)$tokens[$i++];
                    $steps = 16;
                    for ($s = 1; $s <= $steps; $s++) {
                        [$px, $py] = cubic_point($x, $y, $x1, $y1, $x2, $y2, $x3, $y3, $s / $steps);
                        $current[] = [$px, $py];
                    }
                    $x = $x3;
                    $y = $y3;
                }
                break;
            }
            default:
                fwrite(STDERR, "unsupported command: $cmd\n");
                exit(5);
        }
    }

    if ($current) {
        $subpaths[] = $current;
    }
    return $subpaths;
}

function map_point(array $pt, float $minX, float $minY, float $scale, float $offX, float $offY): array {
    return [
        ($pt[0] - $minX) * $scale + $offX,
        ($pt[1] - $minY) * $scale + $offY,
    ];
}

function fill_subpaths_evenodd($im, array $subpaths, int $color): void {
    $allY = [];
    foreach ($subpaths as $poly) {
        foreach ($poly as $pt) {
            $allY[] = $pt[1];
        }
    }
    if (!$allY) {
        return;
    }
    $minY = (int)floor(min($allY));
    $maxY = (int)ceil(max($allY));
    for ($y = $minY; $y <= $maxY; $y++) {
        $intersections = [];
        foreach ($subpaths as $poly) {
            $n = count($poly);
            if ($n < 2) {
                continue;
            }
            for ($i = 0; $i < $n; $i++) {
                $p1 = $poly[$i];
                $p2 = $poly[($i + 1) % $n];
                [$x1, $y1] = $p1;
                [$x2, $y2] = $p2;
                if ($y1 == $y2) {
                    continue;
                }
                if (($y >= min($y1, $y2)) && ($y < max($y1, $y2))) {
                    $x = $x1 + (($y - $y1) * ($x2 - $x1) / ($y2 - $y1));
                    $intersections[] = $x;
                }
            }
        }
        sort($intersections);
        for ($i = 0; $i + 1 < count($intersections); $i += 2) {
            $x1 = (int)floor($intersections[$i]);
            $x2 = (int)ceil($intersections[$i + 1]);
            imageline($im, $x1, $y, $x2, $y, $color);
        }
    }
}

$scale = min($targetW / $vbW, $targetH / $vbH);
$drawW = $vbW * $scale;
$drawH = $vbH * $scale;
$offX = ($targetW - $drawW) / 2.0;
$offY = ($targetH - $drawH) / 2.0;

$im = imagecreatetruecolor($targetW, $targetH);
imagealphablending($im, false);
imagesavealpha($im, true);
$bg = imagecolorallocatealpha($im, 0, 0, 0, 127);
imagefilledrectangle($im, 0, 0, $targetW - 1, $targetH - 1, $bg);
imagealphablending($im, true);

if (preg_match('~^#([0-9a-fA-F]{6})$~', $fillHex, $m)) {
    $rgb = hexdec($m[1]);
    $fill = imagecolorallocate($im, ($rgb >> 16) & 0xff, ($rgb >> 8) & 0xff, $rgb & 0xff);
} else {
    $fill = imagecolorallocate($im, 255, 255, 255);
}

foreach ($paths as $d) {
    $subpaths = svg_parse_path_to_polylines($d);
    $mappedSubpaths = [];
    foreach ($subpaths as $poly) {
        $mapped = [];
        foreach ($poly as $pt) {
            $mapped[] = map_point($pt, $minX, $minY, $scale, $offX, $offY);
        }
        if ($mapped) {
            $mappedSubpaths[] = $mapped;
        }
    }
    fill_subpaths_evenodd($im, $mappedSubpaths, $fill);
}

imagepng($im, $out);
echo "wrote $out\n";
