<?php

if ($argc < 3) {
    fwrite(STDERR, "usage: php sapi/rp2350/tools/phpnet_mirror.php <url> <out-dir>\n");
    exit(1);
}

$url = $argv[1];
$outDir = rtrim($argv[2], '/');

function mirror_fetch(string $url, ?array &$headersOut = null): string {
    $ctx = stream_context_create([
        'http' => [
            'timeout' => 30,
            'header' => "User-Agent: phpnet-mirror/1\r\nConnection: close\r\n",
        ],
    ]);
    $body = @file_get_contents($url, false, $ctx);
    if ($body === false) {
        throw new RuntimeException("fetch failed: $url");
    }
    if (function_exists('http_get_last_response_headers')) {
        $headersOut = http_get_last_response_headers();
    }
    return $body;
}

function mirror_resolve_url(string $baseUrl, string $href): ?string {
    $href = trim(html_entity_decode($href, ENT_QUOTES | ENT_HTML5, 'UTF-8'));
    $href = preg_replace('~#.*$~', '', $href);
    if ($href === '') return null;
    if (preg_match('~^https?://~i', $href)) return $href;
    if (strpos($href, '//') === 0) {
        $parts = parse_url($baseUrl);
        return ($parts['scheme'] ?? 'https') . ':' . $href;
    }
    $parts = parse_url($baseUrl);
    if (!$parts || empty($parts['scheme']) || empty($parts['host'])) return null;
    $root = $parts['scheme'] . '://' . $parts['host'];
    if (!empty($parts['port'])) $root .= ':' . $parts['port'];
    if ($href[0] === '/') return $root . $href;
    if (strpos($href, 'font/') === 0) return $root . '/fonts/Font-Awesome/' . $href;
    if (strpos($href, '../font/') === 0) return $root . '/fonts/Font-Awesome/' . substr($href, 3);
    $basePath = $parts['path'] ?? '/';
    $dir = preg_replace('~/[^/]*$~', '/', $basePath);
    $full = $dir . $href;
    $segs = [];
    foreach (explode('/', $full) as $seg) {
        if ($seg === '' || $seg === '.') continue;
        if ($seg === '..') { array_pop($segs); continue; }
        $segs[] = $seg;
    }
    return $root . '/' . implode('/', $segs);
}

function mirror_local_path(string $rootDir, string $url): string {
    $parts = parse_url($url);
    $host = $parts['host'] ?? 'unknown';
    $path = $parts['path'] ?? '/';
    $query = $parts['query'] ?? '';
    if ($path === '' || substr($path, -1) === '/') {
        $path .= 'index.html';
    }
    $local = $rootDir . '/' . $host . $path;
    if ($query !== '') {
        $local .= '__q_' . rawurlencode($query);
    }
    return $local;
}

function mirror_save(string $path, string $data): void {
    $dir = dirname($path);
    if (!is_dir($dir)) mkdir($dir, 0777, true);
    file_put_contents($path, $data);
}

function mirror_extract_html_assets(string $html, string $baseUrl): array {
    $urls = [];
    $dom = new DOMDocument();
    libxml_use_internal_errors(true);
    @$dom->loadHTML('<?xml encoding="UTF-8">' . $html, LIBXML_NOERROR | LIBXML_NOWARNING | LIBXML_NONET);
    libxml_clear_errors();
    $xp = new DOMXPath($dom);
    foreach ($xp->query('//link[@href]') as $el) {
        if (!($el instanceof DOMElement)) continue;
        $rel = strtolower(trim($el->getAttribute('rel')));
        $hrefAttr = $el->getAttribute('href');
        if (preg_match('~/fonts?/|fontello\.css~i', $hrefAttr)) {
            continue;
        }
        if (strpos($rel, 'stylesheet') !== false || strpos($rel, 'icon') !== false) {
            $u = mirror_resolve_url($baseUrl, $el->getAttribute('href'));
            if ($u) $urls[$u] = true;
        }
    }
    foreach ($xp->query('//img[@src]') as $el) {
        if (!($el instanceof DOMElement)) continue;
        $u = mirror_resolve_url($baseUrl, $el->getAttribute('src'));
        if ($u) $urls[$u] = true;
    }
    return array_keys($urls);
}

function mirror_extract_css_assets(string $css, string $baseUrl): array {
    $urls = [];
    if (preg_match_all('~url\(([^)]+)\)~i', $css, $m)) {
        foreach ($m[1] as $raw) {
            $raw = trim($raw, " \t\r\n\"'");
            if ($raw === '' || stripos($raw, 'data:') === 0) continue;
            if (preg_match('~fontello|/font/|/fonts?/|\.(woff2?|ttf|otf|eot)(\?|$)~i', $raw)) continue;
            $u = mirror_resolve_url($baseUrl, $raw);
            if ($u) $urls[$u] = true;
        }
    }
    return array_keys($urls);
}

$queue = [$url];
$seen = [];
$cssQueue = [];

while ($queue) {
    $cur = array_shift($queue);
    if (isset($seen[$cur])) continue;
    $seen[$cur] = true;
    $headers = null;
    $body = mirror_fetch($cur, $headers);
    $local = mirror_local_path($outDir, $cur);
    mirror_save($local, $body);
    fwrite(STDERR, "saved $cur -> $local\n");

    $contentType = null;
    if (is_array($headers)) {
        foreach ($headers as $h) {
            if (stripos($h, 'Content-Type:') === 0) {
                $contentType = trim(substr($h, 13));
                break;
            }
        }
    }

    $isHtml = preg_match('~\.html?$~i', parse_url($cur, PHP_URL_PATH) ?? '') || ($contentType && stripos($contentType, 'text/html') !== false);
    $isCss = preg_match('~\.css$~i', parse_url($cur, PHP_URL_PATH) ?? '') || ($contentType && stripos($contentType, 'text/css') !== false);

    if ($isHtml) {
        foreach (mirror_extract_html_assets($body, $cur) as $u) {
            if (!isset($seen[$u])) $queue[] = $u;
        }
    }
    if ($isCss) {
        foreach (mirror_extract_css_assets($body, $cur) as $u) {
            if (!isset($seen[$u])) $queue[] = $u;
        }
    }
}
