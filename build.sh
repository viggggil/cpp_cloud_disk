#!/usr/bin/env sh
set -eu

make clean
make -j"$(nproc)"
