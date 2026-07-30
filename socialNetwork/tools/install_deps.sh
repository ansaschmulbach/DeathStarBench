#!/bin/bash
# Installs everything needed to build UniqueIdService and MediaService
# (this branch's ghOSt-FIFO-scheduling experiment binaries) from scratch on
# a plain Ubuntu/Debian box -- no Docker, no ghOSt kernel required. These
# two targets are intentionally minimal: the top-level src/CMakeLists.txt on
# this branch only builds them (see its header comment), so this script only
# installs what THEY need, not what the full 11-service socialNetwork app
# needs (no MongoDB/Redis/AMQP/memcached/jaeger toolchains).
#
# Usage: ./tools/install_deps.sh
# Then, from socialNetwork/:
#   mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && \
#     make -j"$(nproc)" UniqueIdService MediaService
set -euo pipefail

if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
else
  SUDO="sudo"
fi

$SUDO apt-get update
$SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential \
  cmake \
  git \
  wget \
  curl \
  ca-certificates \
  pkg-config \
  libthrift-dev \
  thrift-compiler \
  nlohmann-json3-dev \
  libboost-all-dev \
  libssl-dev

# Ubuntu's packaged libthrift-dev (as of at least 0.13.0-2build2 on 20.04) is
# missing thrift/stdcxx.h, a small compatibility shim upstream Thrift ships
# starting around 0.11. Without it, anything including <thrift/Thrift.h>
# transitively fails to compile. Fetch the shim directly if it's absent.
STDCXX_H=/usr/include/thrift/stdcxx.h
if [ ! -f "$STDCXX_H" ]; then
  echo "Installing missing $STDCXX_H (not shipped by the Ubuntu package)..."
  TMP=$(mktemp)
  curl -fsSL "https://raw.githubusercontent.com/apache/thrift/v0.12.0/lib/cpp/src/thrift/stdcxx.h" -o "$TMP"
  $SUDO cp "$TMP" "$STDCXX_H"
  rm -f "$TMP"
else
  echo "$STDCXX_H already present, skipping."
fi

echo "Done. Build with:"
echo "  mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j\$(nproc) UniqueIdService MediaService"
echo
echo "Note: GHOST_ENCLAVE_TASKS/GHOST_SKIP_YIELD (see src/utils.h and"
echo "src/utils_thrift.h) only matter on a ghOSt kernel. On a stock kernel"
echo "MaybeJoinGhostEnclave() is a no-op (env var unset) and the binaries"
echo "just run under the normal scheduler."
