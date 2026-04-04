<?php

function html2text_fetch_url(string $url): string {
    $ctx = stream_context_create([
        'http' => [
            'timeout' => 20,
            'header' => "User-Agent: html2text-proto/1\r\nAccept: text/html,text/css,*/*\r\nConnection: close\r\n",
        ],
    ]);
    $body = @file_get_contents($url, false, $ctx);
    if ($body === false) {
        throw new RuntimeException("fetch failed: $url");
    }
    return $body;
}

function html2text_fetch_input(string $input): string {
    if (preg_match('~^https?://~i', $input)) {
        return html2text_fetch_url($input);
    }

    $html = @file_get_contents($input);
    if ($html === false) {
        throw new RuntimeException("read failed: $input");
    }
    return $html;
}

function html2text_parse_css_rules(string $css): array {
    $rules = [];
    $css = preg_replace('~/\*.*?\*/~s', '', $css);
    if (!preg_match_all('~([^{}]+)\{([^{}]*)\}~', $css, $matches, PREG_SET_ORDER)) {
        return $rules;
    }
    foreach ($matches as $m) {
        $selectorText = trim($m[1]);
        $declText = trim($m[2]);
        if ($selectorText === '' || $declText === '') {
            continue;
        }
        $decls = [];
        foreach (explode(';', $declText) as $decl) {
            $decl = trim($decl);
            if ($decl === '' || strpos($decl, ':') === false) {
                continue;
            }
            [$prop, $value] = array_map('trim', explode(':', $decl, 2));
            $prop = strtolower($prop);
            $value = strtolower($value);
            if (in_array($prop, ['display', 'white-space', 'font-weight', 'background-image'], true)) {
                $decls[$prop] = $value;
            }
        }
        if (!$decls) {
            continue;
        }
        foreach (explode(',', $selectorText) as $selector) {
            $selector = trim($selector);
            if ($selector === '') {
                continue;
            }
            $rules[] = [
                'selector' => $selector,
                'decls' => $decls,
            ];
        }
    }
    return $rules;
}

function html2text_parse_inline_style(string $style): array {
    $decls = [];
    foreach (explode(';', $style) as $decl) {
        $decl = trim($decl);
        if ($decl === '' || strpos($decl, ':') === false) {
            continue;
        }
        [$prop, $value] = array_map('trim', explode(':', $decl, 2));
        $prop = strtolower($prop);
        $value = strtolower($value);
        if (in_array($prop, ['display', 'white-space', 'font-weight', 'background-image'], true)) {
            $decls[$prop] = $value;
        }
    }
    return $decls;
}

function html2text_node_matches_simple_selector(DOMElement $el, string $selector): bool {
    $selector = trim($selector);
    if ($selector === '') {
        return false;
    }
    if (preg_match('~^[a-zA-Z][a-zA-Z0-9_-]*$~', $selector)) {
        return strcasecmp($el->tagName, $selector) === 0;
    }
    if ($selector[0] === '.') {
        $class = substr($selector, 1);
        $classes = preg_split('~\s+~', trim($el->getAttribute('class')));
        return in_array($class, $classes, true);
    }
    if ($selector[0] === '#') {
        return $el->getAttribute('id') === substr($selector, 1);
    }
    if (preg_match('~^([a-zA-Z][a-zA-Z0-9_-]*)\.([a-zA-Z0-9_-]+)$~', $selector, $m)) {
        if (strcasecmp($el->tagName, $m[1]) !== 0) {
            return false;
        }
        $classes = preg_split('~\s+~', trim($el->getAttribute('class')));
        return in_array($m[2], $classes, true);
    }
    return false;
}

function html2text_node_matches_selector(DOMElement $el, string $selector): bool {
    $parts = preg_split('~\s+~', trim($selector));
    if (!$parts) {
        return false;
    }
    $part = array_pop($parts);
    if (!html2text_node_matches_simple_selector($el, $part)) {
        return false;
    }
    $current = $el->parentNode;
    while ($parts && $current instanceof DOMElement) {
        $wanted = array_pop($parts);
        while ($current instanceof DOMElement && !html2text_node_matches_simple_selector($current, $wanted)) {
            $current = $current->parentNode;
        }
        if (!($current instanceof DOMElement)) {
            return false;
        }
        $current = $current->parentNode;
    }
    return !$parts;
}

function html2text_compute_style(DOMNode $node, array $cssRules): array {
    $style = [
        'display' => null,
        'white-space' => null,
        'font-weight' => null,
        'background-image' => null,
    ];
    if (!($node instanceof DOMElement)) {
        return $style;
    }
    foreach ($cssRules as $rule) {
        if (html2text_node_matches_selector($node, $rule['selector'])) {
            foreach ($rule['decls'] as $k => $v) {
                $style[$k] = $v;
            }
        }
    }
    if ($node->hasAttribute('style')) {
        foreach (html2text_parse_inline_style($node->getAttribute('style')) as $k => $v) {
            $style[$k] = $v;
        }
    }
    return $style;
}

function html2text_append_text_line(array &$out, string $text): void {
    if ($text === '') {
        return;
    }
    if (empty($out)) {
        $out[] = $text;
        return;
    }
    $last = count($out) - 1;
    $out[$last] .= $text;
}

function html2text_append_break(array &$out, int $count = 1): void {
    for ($i = 0; $i < $count; $i++) {
        if (empty($out) || end($out) !== '') {
            $out[] = '';
        }
    }
}

function html2text_render_dom_node(DOMNode $node, array $cssRules, array &$out, array $ctx = []): void {
    $ctx += [
        'pre' => false,
        'hidden' => false,
        'li_depth' => 0,
        'link_href' => null,
    ];

    if ($ctx['hidden']) {
        return;
    }

    if ($node instanceof DOMText) {
        $text = $node->nodeValue;
        if ($ctx['pre']) {
            $text = str_replace("\r", '', $text);
            $lines = explode("\n", $text);
            foreach ($lines as $idx => $line) {
                $line = html_entity_decode($line, ENT_QUOTES | ENT_HTML5, 'UTF-8');
                $line = str_replace("\xc2\xa0", ' ', $line);
                if ($idx > 0) {
                    html2text_append_break($out, 1);
                }
                html2text_append_text_line($out, $line);
            }
            return;
        }
        $text = html_entity_decode($text, ENT_QUOTES | ENT_HTML5, 'UTF-8');
        $text = str_replace("\xc2\xa0", ' ', $text);
        $text = preg_replace('~\s+~u', ' ', $text);
        if ($text === '' || $text === null) {
            return;
        }
        html2text_append_text_line($out, $text);
        return;
    }

    if (!($node instanceof DOMElement)) {
        return;
    }

    $tag = strtolower($node->tagName);
    if (in_array($tag, ['script', 'style', 'noscript', 'svg'], true)) {
        return;
    }

    $style = html2text_compute_style($node, $cssRules);
    if (($style['display'] ?? null) === 'none') {
        return;
    }

    $blockTags = ['div', 'p', 'section', 'article', 'main', 'header', 'footer', 'aside', 'nav', 'ul', 'ol', 'li', 'pre', 'table', 'tr', 'td', 'th'];
    $headingTags = ['h1', 'h2', 'h3', 'h4', 'h5', 'h6'];
    $isBlock = in_array($tag, $blockTags, true) || in_array($tag, $headingTags, true);
    $isHeading = in_array($tag, $headingTags, true);
    $isPre = $ctx['pre'] || $tag === 'pre' || (($style['white-space'] ?? null) === 'pre');

    if ($tag === 'br' || $tag === 'hr') {
        html2text_append_break($out, 1);
        return;
    }

    if ($isBlock) {
        html2text_append_break($out, $isHeading ? 2 : 1);
    }

    if ($tag === 'li') {
        html2text_append_text_line($out, str_repeat('  ', max(0, $ctx['li_depth'] - 1)) . '* ');
    }

    $nextCtx = $ctx;
    $nextCtx['pre'] = $isPre;
    if ($tag === 'ul' || $tag === 'ol') {
        $nextCtx['li_depth'] = $ctx['li_depth'] + 1;
    }
    if ($tag === 'a' && $node->hasAttribute('href')) {
        $nextCtx['link_href'] = $node->getAttribute('href');
    }

    foreach ($node->childNodes as $child) {
        html2text_render_dom_node($child, $cssRules, $out, $nextCtx);
    }

    if ($isBlock) {
        html2text_append_break($out, 1);
    }
}

function html2text_normalize_text(string $text): string {
    $text = str_replace("\xc2\xa0", ' ', $text);
    $text = str_replace('·', ' * ', $text);
    if (function_exists('iconv')) {
        $converted = @iconv('UTF-8', 'ASCII//TRANSLIT//IGNORE', $text);
        if (is_string($converted) && $converted !== '') {
            $text = $converted;
        }
    }
    $text = preg_replace('~[^\x09\x0a\x0d\x20-\x7e]~', ' ', $text);
    $text = preg_replace('~\s+\*\s+~', ' * ', $text);
    $text = preg_replace('~[ ]+~', ' ', $text);
    return trim($text);
}

function html2text_wrap_lines(array $lines, int $width, int $maxLines): array {
    $out = [];
    foreach ($lines as $line) {
        $line = html2text_normalize_text($line);
        if ($line === '') {
            if (!empty($out) && end($out) !== '') {
                $out[] = '';
            }
            if (count($out) >= $maxLines) {
                break;
            }
            continue;
        }
        $words = preg_split('~\s+~u', trim($line));
        $cur = '';
        foreach ($words as $word) {
            if ($word === '') {
                continue;
            }
            if ($cur === '') {
                $cur = $word;
            } else {
                $candidate = $cur . ' ' . $word;
                if (mb_strlen($candidate, 'UTF-8') <= $width) {
                    $cur = $candidate;
                } else {
                    $out[] = $cur;
                    if (count($out) >= $maxLines) {
                        break 2;
                    }
                    $cur = $word;
                }
            }
        }
        if ($cur !== '') {
            $out[] = $cur;
            if (count($out) >= $maxLines) {
                break;
            }
        }
    }
    while (!empty($out) && end($out) === '') {
        array_pop($out);
    }
    return array_slice($out, 0, $maxLines);
}

function html2text_resolve_url(string $baseUrl, string $href): ?string {
    $href = trim($href);
    if ($href === '') {
        return null;
    }
    if (preg_match('~^https?://~i', $href)) {
        return $href;
    }
    if (strpos($href, '//') === 0) {
        $parts = parse_url($baseUrl);
        if (!$parts || empty($parts['scheme'])) {
            return null;
        }
        return $parts['scheme'] . ':' . $href;
    }
    $parts = parse_url($baseUrl);
    if (!$parts || empty($parts['scheme']) || empty($parts['host'])) {
        return null;
    }
    $scheme = $parts['scheme'];
    $host = $parts['host'];
    $port = isset($parts['port']) ? ':' . $parts['port'] : '';
    $basePath = $parts['path'] ?? '/';
    if ($href[0] === '/') {
        return $scheme . '://' . $host . $port . $href;
    }
    $dir = preg_replace('~/[^/]*$~', '/', $basePath);
    $full = $dir . $href;
    $segs = [];
    foreach (explode('/', $full) as $seg) {
        if ($seg === '' || $seg === '.') {
            continue;
        }
        if ($seg === '..') {
            array_pop($segs);
            continue;
        }
        $segs[] = $seg;
    }
    return $scheme . '://' . $host . $port . '/' . implode('/', $segs);
}

function html2text_load_external_css(DOMXPath $xpath, string $baseUrl): array {
    $rules = [];
    $seen = [];
    foreach ($xpath->query('//link[@rel]') as $link) {
        if (!($link instanceof DOMElement)) {
            continue;
        }
        $rel = strtolower(trim($link->getAttribute('rel')));
        if (strpos($rel, 'stylesheet') === false) {
            continue;
        }
        $href = trim($link->getAttribute('href'));
        if ($href === '') {
            continue;
        }
        $url = html2text_resolve_url($baseUrl, $href);
        if ($url === null || isset($seen[$url])) {
            continue;
        }
        $seen[$url] = true;
        try {
            $css = html2text_fetch_url($url);
            $rules = array_merge($rules, html2text_parse_css_rules($css));
        } catch (Throwable $e) {
        }
    }
    return $rules;
}

function html2text_extract_dom_and_root(string $html, ?string $baseUrl = null): array {
    $dom = new DOMDocument();
    libxml_use_internal_errors(true);
    @$dom->loadHTML('<?xml encoding="UTF-8">' . $html, LIBXML_NOERROR | LIBXML_NOWARNING | LIBXML_NONET);
    libxml_clear_errors();

    $xpath = new DOMXPath($dom);
    $cssRules = [];
    foreach ($xpath->query('//style') as $styleNode) {
        $cssRules = array_merge($cssRules, html2text_parse_css_rules($styleNode->textContent));
    }
    if (is_string($baseUrl) && preg_match('~^https?://~i', $baseUrl)) {
        $cssRules = array_merge($cssRules, html2text_load_external_css($xpath, $baseUrl));
    }

    $root = null;
    foreach ([
        '//*[contains(concat(" ", normalize-space(@class), " "), " hero ")]',
        '//*[@id="intro"]',
        '//main',
        '//article',
        '//*[@role="main"]',
        '//body'
    ] as $query) {
        $n = $xpath->query($query);
        if ($n && $n->length > 0) {
            $root = $n->item(0);
            break;
        }
    }

    return [$dom, $xpath, $root, $cssRules];
}

function html2text_render_lines(string $html, int $width = 34, int $maxLines = 14, ?string $baseUrl = null): array {
    [$dom, $xpath, $root, $cssRules] = html2text_extract_dom_and_root($html, $baseUrl);
    if (!$root) {
        return ['no render root found'];
    }

    $rawLines = [];
    html2text_render_dom_node($root, $cssRules, $rawLines, []);
    return html2text_wrap_lines($rawLines, $width, $maxLines);
}

function html2text_find_background_image_info(string $html, ?string $baseUrl = null): ?array {
    [$dom, $xpath, $root, $cssRules] = html2text_extract_dom_and_root($html, $baseUrl);
    if (!$root || !($root instanceof DOMElement)) {
        return null;
    }
    $candidates = [$root, $root->parentNode];
    foreach (['//html', '//body'] as $query) {
        $n = $xpath->query($query);
        if ($n && $n->length > 0) {
            $candidates[] = $n->item(0);
        }
    }
    foreach ($candidates as $node) {
        if (!($node instanceof DOMElement)) {
            continue;
        }
        $style = html2text_compute_style($node, $cssRules);
        $bg = $style['background-image'] ?? null;
        if (!is_string($bg) || $bg === '' || $bg === 'none') {
            continue;
        }
        if (!preg_match('~url\(([^)]+)\)~i', $bg, $m)) {
            continue;
        }
        $src = trim($m[1], "'\" ");
        if ($src === '') {
            continue;
        }
        $url = is_string($baseUrl) ? html2text_resolve_url($baseUrl, $src) : $src;
        if (!$url) {
            continue;
        }
        return [
            'src' => $src,
            'url' => $url,
        ];
    }
    return null;
}

function html2text_find_first_image_info(string $html, ?string $baseUrl = null): ?array {
    [$dom, $xpath, $root, $cssRules] = html2text_extract_dom_and_root($html, $baseUrl);
    if (!$root || !($root instanceof DOMElement)) {
        return null;
    }
    foreach ($xpath->query('.//img[@src]', $root) as $img) {
        if (!($img instanceof DOMElement)) {
            continue;
        }
        $style = html2text_compute_style($img, $cssRules);
        if (($style['display'] ?? null) === 'none') {
            continue;
        }
        $src = trim($img->getAttribute('src'));
        if ($src === '') {
            continue;
        }
        $url = is_string($baseUrl) ? html2text_resolve_url($baseUrl, $src) : $src;
        if (!$url) {
            continue;
        }
        return [
            'src' => $src,
            'url' => $url,
            'alt' => $img->getAttribute('alt'),
            'class' => $img->getAttribute('class'),
        ];
    }
    return null;
}
