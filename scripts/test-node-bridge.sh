#!/usr/bin/env bash
# Optional host Node-API check. Supply official matching Node headers locally.
# This does not validate Harmony's Node-API headers, ArkTS, or HAP packaging.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${NODE_HEADERS:?Set NODE_HEADERS to the official Node source src directory containing node_api.h}"
test -f "$NODE_HEADERS/node_api.h"
BUILD="${COMPANION_HOST_BUILD:-$ROOT/build/host}"
mkdir -p "$BUILD"
read -r -a SSL_CFLAGS <<< "$(pkg-config --cflags libcrypto)"
read -r -a SSL_LIBS <<< "$(pkg-config --libs libcrypto)"
PLATFORM_LINK=()
if [[ "$(uname -s)" == Darwin ]]; then
  PLATFORM_LINK=(-Wl,-undefined,dynamic_lookup)
fi
"${CXX:-c++}" -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread -fPIC -shared \
  -DCOMPANION_NODE_HOST -I"$NODE_HEADERS" -I"$ROOT/native/include" "${SSL_CFLAGS[@]}" \
  "$ROOT/native/src/crypto.cpp" "$ROOT/native/src/codec.cpp" "$ROOT/native/src/session.cpp" \
  "$ROOT/native/bridge/napi.cpp" "${SSL_LIBS[@]}" "${PLATFORM_LINK[@]}" -o "$BUILD/companion.node"
node "$ROOT/scripts/test-node-bridge.cjs" "$BUILD/companion.node"
node "$ROOT/scripts/test-node-teardown.cjs" "$BUILD/companion.node"
