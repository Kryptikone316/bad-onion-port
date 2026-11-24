#!/bin/bash
# qrun.sh <rundir> [args...]: run fxi.mipsel in <rundir> under qemu-user (via the host), env from the caller
RD="$(cd "$1" && pwd)"; shift
SYS=$HOME/handhelds/sf3000-work/buildroot/output/host/mipsel-buildroot-linux-gnu/sysroot
envs=()
for v in SF3500_KEYSCRIPT SF3500_SHOT_PREFIX SF3500_SHOT_FRAMES SF3500_QUIT_FRAME; do [ -n "${!v:-}" ] && envs+=("--env=$v=${!v}"); done
flatpak-spawn --host --directory="$RD" "${envs[@]}" qemu-mipsel-static -L "$SYS" ./fxi.mipsel "$@"
