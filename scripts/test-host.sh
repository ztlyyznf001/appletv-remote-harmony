#!/usr/bin/env bash
# No download, package installation, SDK, signing, or network device access.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${COMPANION_HOST_BUILD:-$ROOT/build/host}"
CXX="${CXX:-c++}"
mkdir -p "$BUILD"
read -r -a SSL_CFLAGS <<< "$(pkg-config --cflags libcrypto)"
read -r -a SSL_LIBS <<< "$(pkg-config --libs libcrypto)"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread -I"$ROOT/native/include")
if [[ "${SANITIZE:-0}" == 1 ]]; then
  FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
SOURCES=("$ROOT/native/src/crypto.cpp" "$ROOT/native/src/codec.cpp" "$ROOT/native/src/session.cpp")
"$CXX" --version | head -n 1
printf 'OpenSSL (host pkg-config): %s\n' "$(pkg-config --modversion libcrypto)"
for TEST in crypto_codec_test session_test; do
  "$CXX" "${FLAGS[@]}" "${SSL_CFLAGS[@]}" "${SOURCES[@]}" "$ROOT/native/tests/$TEST.cpp" "${SSL_LIBS[@]}" -o "$BUILD/$TEST"
  "$BUILD/$TEST"
done
printf '\nPASS: host C++ tests. Harmony/ArkTS/HAP and Apple TV hardware were NOT tested by this command.\n'
