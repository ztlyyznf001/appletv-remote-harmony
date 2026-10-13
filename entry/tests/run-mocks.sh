#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
node lifecycle-discovery.mock.mjs
node credential-store.mock.mjs
