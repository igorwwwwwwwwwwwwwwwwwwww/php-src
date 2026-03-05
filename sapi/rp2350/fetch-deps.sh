#!/usr/bin/env bash
set -euo pipefail

FIRMWARE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="${FIRMWARE_DIR}/third_party"

PICO_SDK_REF="${PICO_SDK_REF:-master}"
PICOTOOL_REF="${PICOTOOL_REF:-master}"
NGHTTP2_REF="${NGHTTP2_REF:-master}"

mkdir -p "${THIRD_PARTY_DIR}"

if [[ ! -d "${THIRD_PARTY_DIR}/pico-sdk/.git" ]]; then
  git clone --depth 1 --branch "${PICO_SDK_REF}" https://github.com/raspberrypi/pico-sdk.git "${THIRD_PARTY_DIR}/pico-sdk"
else
  git -C "${THIRD_PARTY_DIR}/pico-sdk" fetch origin "${PICO_SDK_REF}" --depth 1
  git -C "${THIRD_PARTY_DIR}/pico-sdk" checkout FETCH_HEAD
fi
git -C "${THIRD_PARTY_DIR}/pico-sdk" submodule update --init

if [[ ! -d "${THIRD_PARTY_DIR}/picotool/.git" ]]; then
  git clone --depth 1 --branch "${PICOTOOL_REF}" https://github.com/raspberrypi/picotool.git "${THIRD_PARTY_DIR}/picotool"
else
  git -C "${THIRD_PARTY_DIR}/picotool" fetch origin "${PICOTOOL_REF}" --depth 1
  git -C "${THIRD_PARTY_DIR}/picotool" checkout FETCH_HEAD
fi

if [[ ! -d "${THIRD_PARTY_DIR}/nghttp2/.git" ]]; then
  git clone --depth 1 --branch "${NGHTTP2_REF}" https://github.com/nghttp2/nghttp2.git "${THIRD_PARTY_DIR}/nghttp2"
else
  git -C "${THIRD_PARTY_DIR}/nghttp2" fetch origin "${NGHTTP2_REF}" --depth 1
  git -C "${THIRD_PARTY_DIR}/nghttp2" checkout FETCH_HEAD
fi

cat <<'MSG'
Dependencies are available in:
  sapi/rp2350/third_party/pico-sdk
  sapi/rp2350/third_party/picotool
  sapi/rp2350/third_party/nghttp2

Next:
  cmake -S sapi/rp2350 -B sapi/rp2350/build -DPICO_BOARD=pico2
  cmake --build sapi/rp2350/build -j
MSG
