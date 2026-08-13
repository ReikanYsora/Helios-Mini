#!/usr/bin/env bash
# Host unit tests for Helios Mini's pure math (no ESP-IDF, no hardware).
# Compiles the firmware's own pure sources with the desktop toolchain and
# runs the assertions. Exit code is non-zero if any check fails.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$HERE/../../firmware"

cc -std=c11 -Wall -Wextra -O2 \
    -I"$FW/helios/energy_math/include" \
    -I"$FW/helios/irradiance_model/include" \
    "$HERE/test_energy_math.c" \
    "$FW/helios/energy_math/energy_math.c" \
    "$FW/helios/irradiance_model/sun_math.c" \
    -lm -o "$HERE/helios_tests"

"$HERE/helios_tests"
