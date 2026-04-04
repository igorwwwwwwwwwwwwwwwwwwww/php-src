#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/certs/mozilla-cacert.pem"
SEL="$ROOT/certs/mozilla-selected.pem"
OUT_C="$ROOT/src/rp2350_ca_bundle.c"
OUT_H="$ROOT/src/rp2350_ca_bundle.h"
if [[ ! -f "$SRC" ]]; then
  curl -fsSL https://curl.se/ca/cacert.pem -o "$SRC"
fi

# Keep a small, practical subset from Mozilla roots to fit MCU memory budgets.
# Extend this list as needed.
mapfile -t keep_subjects <<'SUBJ'
SSL.com TLS ECC Root CA 2022
SSL.com TLS Transit ECC CA R2
AAA Certificate Services
ISRG Root X1
ISRG Root X2
SUBJ

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT
extract_certs() {
  local in="$1"
  local out="$2"
  mkdir -p "$out"
  awk -v out="$out" '
BEGIN { n=0; in_cert=0; fn="" }
/-----BEGIN CERTIFICATE-----/ {
  n++;
  fn = sprintf("%s/cert_%04d.pem", out, n);
  in_cert=1;
  print > fn;
  next;
}
{
  if (in_cert) {
    print > fn;
    if ($0 ~ /-----END CERTIFICATE-----/) {
      in_cert=0;
      fn="";
    }
  }
}
  ' "$in"
}

extract_certs "$SRC" "$TMPDIR/mozilla"
: > "$SEL"
declare -A found_subjects=()
for f in "$TMPDIR"/mozilla/cert_*.pem; do
  subj=$(openssl x509 -in "$f" -noout -subject 2>/dev/null || true)
  for k in "${keep_subjects[@]}"; do
    if [[ "$subj" == *"$k"* ]]; then
      cat "$f" >> "$SEL"
      found_subjects["$k"]=1
      break
    fi
  done
done

# Fallback for subjects missing from Mozilla bundle (e.g. some platform-trusted roots).
# On macOS, search the system root keychain by subject name.
if command -v security >/dev/null 2>&1; then
  APPLE_SRC="$TMPDIR/apple-system-roots.pem"
  security find-certificate -a -p /System/Library/Keychains/SystemRootCertificates.keychain > "$APPLE_SRC" || true
  if [[ -s "$APPLE_SRC" ]]; then
    extract_certs "$APPLE_SRC" "$TMPDIR/apple"
    for k in "${keep_subjects[@]}"; do
      if [[ -n "${found_subjects[$k]:-}" ]]; then
        continue
      fi
      for f in "$TMPDIR"/apple/cert_*.pem; do
        subj=$(openssl x509 -in "$f" -noout -subject 2>/dev/null || true)
        if [[ "$subj" == *"$k"* ]]; then
          cat "$f" >> "$SEL"
          found_subjects["$k"]=1
          break
        fi
      done
    done
  fi
fi

# Fallback for explicitly vendored certs in certs/extra.
EXTRA_DIR="$ROOT/certs/extra"
if [[ -d "$EXTRA_DIR" ]]; then
  for k in "${keep_subjects[@]}"; do
    if [[ -n "${found_subjects[$k]:-}" ]]; then
      continue
    fi
    for f in "$EXTRA_DIR"/*.pem; do
      [[ -f "$f" ]] || continue
      subj=$(openssl x509 -in "$f" -noout -subject 2>/dev/null || true)
      if [[ "$subj" == *"$k"* ]]; then
        cat "$f" >> "$SEL"
        found_subjects["$k"]=1
        break
      fi
    done
  done
fi

for k in "${keep_subjects[@]}"; do
  if [[ -z "${found_subjects[$k]:-}" ]]; then
    echo "warning: subject not found in Mozilla/system roots: $k" >&2
  fi
done

{
  echo "#ifndef RP2350_CA_BUNDLE_H"
  echo "#define RP2350_CA_BUNDLE_H"
  echo
  echo "#include <stddef.h>"
  echo
  echo "extern const unsigned char rp2350_ca_bundle_pem[];"
  echo "extern const size_t rp2350_ca_bundle_pem_len;"
  echo
  echo "#endif"
} > "$OUT_H"

{
  echo "#include <stddef.h>"
  echo "#include \"rp2350_ca_bundle.h\""
  echo
  echo "/* Source roots selected from Mozilla CA bundle: certs/mozilla-cacert.pem */"
  echo "const unsigned char rp2350_ca_bundle_pem[] ="
  sed 's/\\/\\\\/g; s/\"/\\\"/g; s/$/\\n/' "$SEL" | sed 's/^/    "/; s/$/"/'
  echo "    \"\";"
  echo
  echo "const size_t rp2350_ca_bundle_pem_len = sizeof(rp2350_ca_bundle_pem);"
} > "$OUT_C"

echo "selected bundle bytes: $(wc -c < "$SEL")"
