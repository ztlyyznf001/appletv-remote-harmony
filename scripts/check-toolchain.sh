#!/usr/bin/env bash
# Read-only preflight. No automatic downloads, agreement acceptance or signing.
set -euo pipefail
MISSING=0
for TOOL in hvigorw ohpm hdc; do
  if command -v "$TOOL" >/dev/null 2>&1; then
    printf '%s: %s\n' "$TOOL" "$(command -v "$TOOL")"
  else
    printf '%s: not on PATH (DevEco bundled tool paths may need configuration)\n' "$TOOL"
    MISSING=1
  fi
done
if [[ -n "${OHOS_NATIVE_HOME:-}" && -f "$OHOS_NATIVE_HOME/build/cmake/ohos.toolchain.cmake" && -x "$OHOS_NATIVE_HOME/llvm/bin/clang" ]]; then
  printf 'Harmony NDK native root: %s\n' "$OHOS_NATIVE_HOME"
else
  printf 'Harmony NDK: set OHOS_NATIVE_HOME to the official SDK native directory.\n'
  MISSING=1
fi
if [[ -n "${COMPANION_HARMONY_OPENSSL_ROOT:-}" && -f "$COMPANION_HARMONY_OPENSSL_ROOT/include/openssl/evp.h" && -f "$COMPANION_HARMONY_OPENSSL_ROOT/lib/libcrypto.a" ]]; then
  printf 'OpenSSL prefix exists: %s (target ABI still must be verified)\n' "$COMPANION_HARMONY_OPENSSL_ROOT"
else
  printf 'OpenSSL: set COMPANION_HARMONY_OPENSSL_ROOT to the Harmony arm64 target prefix.\n'
  MISSING=1
fi
if [[ "$MISSING" == 1 ]]; then
  printf '\nNOT READY: see docs/DEPLOYMENT.zh-CN.md. Host tests remain available.\n'
  exit 2
fi
printf '\nPreflight paths found; this is not a compilation or signing check.\n'
