#!/usr/bin/env bash
# 用 STM32CubeIDE 的 headless 构建器编译单个应用。
#
#   ./scripts/build.sh face-id
#   CUBEIDE=/opt/st/stm32cubeide/stm32cubeide ./scripts/build.sh timer-bench
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
SRC="$ROOT/apps/$APP/firmware"
if [ ! -d "$SRC" ]; then
  echo "找不到工程目录: $SRC"
  exit 1
fi

if [ -z "${CUBEIDE:-}" ]; then
  for c in \
    "/c/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/stm32cubeide.exe" \
    "/c/ST/STM32CubeIDE_1.16.1/STM32CubeIDE/stm32cubeide.exe" \
    "/opt/st/stm32cubeide/stm32cubeide" \
    "/Applications/STM32CubeIDE.app/Contents/MacOS/stm32cubeide"; do
    if [ -x "$c" ]; then CUBEIDE="$c"; break; fi
  done
fi
if [ -z "${CUBEIDE:-}" ]; then
  echo "未找到 STM32CubeIDE，请设置环境变量 CUBEIDE 指向可执行文件"
  exit 1
fi

# Eclipse 需要本机格式的路径（Windows 下必须是 C:\... 而非 C:/...）
to_native() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$1"
  else
    printf '%s' "$1"
  fi
}

# Eclipse 需要一个干净的一次性 workspace；用 POSIX 路径，避免 Windows 下
# TMPDIR 带盘符时拼出 C:\...\Temp/xxx 这类混合分隔符路径。
WS="/tmp/cubeide-ws.$$"
mkdir -p "$WS"
trap 'rm -rf "$WS" 2>/dev/null || true' EXIT

echo "==> 构建 $APP（工程 $PRJ）"
"$CUBEIDE" -nosplash --launcher.suppressErrors \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "$(to_native "$WS")" \
  -import "$(to_native "$SRC")" \
  -build "$PRJ/Debug"

ELF="$SRC/Debug/$PRJ.elf"
if [ -f "$ELF" ]; then
  echo "==> 完成: apps/$APP/firmware/Debug/$PRJ.elf"
else
  echo "==> 构建失败：未生成 $ELF"
  exit 1
fi
