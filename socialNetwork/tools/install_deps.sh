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

# perf: used to measure per-request cycles/instructions/IPC (see
# tools/run_with_perf.sh). Ubuntu ships perf as a kernel-version-specific
# package. DO NOT `apt-get install "linux-tools-$(uname -r)"` -- on a
# custom kernel string (e.g. a ghOSt kernel, or anything apt doesn't have an
# exact package for) this doesn't cleanly fail "unable to locate package"
# the way you'd expect: apt's fuzzy name matching can instead go unpack
# every package that merely shares the version-number prefix (every
# unrelated cloud-flavor kernel-tools package: aws/azure/gcp/oracle, every
# point release, ...), leaving the system with dozens of half-installed
# packages and a broken `apt-get check`. Only ever install the generic
# metapackage; if /usr/bin/perf's own version-dispatch wrapper can't find a
# kernel-matched binary, symlink whatever generic one DID get installed --
# perf's basic hardware-counter `stat` mode doesn't need an exact match.
$SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y \
  linux-tools-common linux-tools-generic

if ! command -v perf >/dev/null 2>&1 || ! perf --version >/dev/null 2>&1; then
  FALLBACK_PERF=$(find /usr/lib/linux-tools-* -maxdepth 1 -name perf 2>/dev/null | head -1)
  if [ -n "$FALLBACK_PERF" ]; then
    echo "No kernel-matched perf; symlinking $FALLBACK_PERF -> /usr/local/bin/perf"
    $SUDO ln -sf "$FALLBACK_PERF" /usr/local/bin/perf
    hash -r  # forget this shell's cached (kernel-mismatched) `perf` lookup
  else
    echo "WARNING: could not find any perf binary to use." >&2
  fi
fi
perf --version || true

echo "Done. Build with:"
echo "  mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j\$(nproc) UniqueIdService MediaService"
echo
echo "Note: GHOST_ENCLAVE_TASKS/GHOST_SKIP_YIELD (see src/utils.h and"
echo "src/utils_thrift.h) only matter on a ghOSt kernel. On a stock kernel"
echo "MaybeJoinGhostEnclave() is a no-op (env var unset) and the binaries"
echo "just run under the normal scheduler."
