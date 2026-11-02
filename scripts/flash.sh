#!/usr/bin/env bash
# 用 OpenOCD + CMSIS-DAP 烧录单个应用的固件。
#
#   ./scripts/flash.sh face-id
#   OPENOCD="/c/ST/.../openocd.exe" ./scripts/flash.sh led-marquee
#
set -euo pipefail

usage() {
  echo "用法: $0 <app>"
  echo "  app: led-marquee | timer-bench | home-hub | face-id"
  exit 2
}

APP="${1:-}"
[ -n "$APP" ] || usage

case "$APP" in
  led-marquee) PRJ=exp1 ;;
  timer-bench) PRJ=exp2 ;;
  home-hub)    PRJ=exp3 ;;
  face-id)     PRJ=exp4 ;;
  *) echo "未知应用: $APP"; usage ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/apps/$APP/firmware"
ELF="$DIR/Debug/$PRJ.elf"

if [ ! -f "$ELF" ]; then
  echo "找不到固件: apps/$APP/firmware/Debug/$PRJ.elf"
  echo "请先构建：./scripts/build.sh $APP"
  exit 1
fi

if [ -z "${OPENOCD:-}" ]; then
  for c in \
    "/c/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.openocd.win32_*/tools/bin/openocd.exe" \
    "/opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.openocd.linux*/tools/bin/openocd"; do
    for f in $c; do
      if [ -x "$f" ]; then OPENOCD="$f"; break 2; fi
    done
  done
fi
OPENOCD="${OPENOCD:-openocd}"

echo "==> 烧录 $APP"
"$OPENOCD" -f "$DIR/dap.cfg" -c "program \"$ELF\" verify reset exit"
echo "==> 完成"
