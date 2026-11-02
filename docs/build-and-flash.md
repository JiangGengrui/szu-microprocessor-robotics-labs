# 构建、烧录与排障

## 1. 工具链

| 工具 | 版本 | 用途 |
|------|------|------|
| STM32CubeIDE | 2.2.0（1.16.1 亦可） | 工程配置、编译、调试；内置 arm-none-eabi-gcc 与 OpenOCD |
| arm-none-eabi-gcc | 随 IDE | 交叉编译（`-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`） |
| OpenOCD | 随 IDE | CMSIS-DAP 烧录 |
| CMSIS-DAP | 任意 DAP-Link 固件 | 调试探针 |

四个工程分别名为 `exp1`（led-marquee）、`exp2`（timer-bench）、`exp3`（home-hub）、`exp4`（face-id），目录内的 `.project` 已固定该名称。

## 2. 构建

### 2.1 IDE

1. `File > Import > General > Existing Projects into Workspace`；
2. 选择 `apps/<app>/firmware`；
3. `Project > Build Project`（Ctrl+B）。

产物在 `firmware/Debug/`：`expN.elf`、`.map`、`.list`。该目录已在 `.gitignore` 中排除，克隆后首次构建由 IDE 生成。

### 2.2 命令行（headless）

```bash
export CUBEIDE="/c/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/stm32cubeide.exe"
./scripts/build.sh face-id
```

脚本内部：

```bash
"$CUBEIDE" -nosplash --launcher.suppressErrors \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data "<临时 workspace>" -import "<工程绝对路径>" -build "exp4/Debug"
```

两个易踩的点：

- `-data` / `-import` 后面必须是 **Windows 路径**（`C:\...`）。在 Git Bash 下用 `cygpath -w` 转换，脚本已处理；直接传 `C:/...` 会被 Eclipse 当成 URI scheme 报 `No file system is defined for scheme: C`。
- headless 构建不依赖 IDE 打开过工程，但第一次会较慢（生成 makefile 与索引）。

## 3. 烧录

```bash
# 默认使用 PATH 中的 openocd
./scripts/flash.sh face-id

# 或指定 CubeIDE 自带的 openocd
export OPENOCD="/c/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/.../openocd.exe"
./scripts/flash.sh face-id
```

等价命令：

```bash
openocd -f apps/face-id/firmware/dap.cfg \
  -c "program apps/face-id/firmware/Debug/exp4.elf verify reset exit"
```

`dap.cfg` 已包含 `interface cmsis-dap` 与 `target stm32f4x`，无需额外指定。

## 4. 调试

- IDE 内直接 `Run > Debug`，工程已带 `expN Debug.launch`。
- 常用断点位置：`main.c` 的初始化分支（确认外设 init 返回值）、DMA 传输完成回调（确认行计数递增）、识别入口（确认查询图指针）。
- 观察内存：CubeIDE 的 Live Expressions 可直接看 `0xC0000000` 起的帧缓冲内容（以 `uint16_t` 数组形式）。

## 5. 常见问题

### 构建

| 现象 | 原因 | 处理 |
|------|------|------|
| `No file system is defined for scheme: C` | headless 传了 `C:/...` | 用 `cygpath -w` 转成 Windows 路径 |
| 编译通过但现象是旧的 | 改了 `.ioc` 未重新生成代码，或运行的是旧 elf | CubeMX 里 Ctrl+S 重新生成，再 Rebuild |
| 自定义代码"消失" | 写在了 `USER CODE` 区外 | 移到 `/* USER CODE BEGIN */ ... END */` 之间 |
| 重新生成后引脚被改 | CubeMX 把引脚分配给了别的外设 | 从 `.ioc` 移除无关外设，或该引脚改由用户代码初始化 |

### 烧录

| 现象 | 原因 | 处理 |
|------|------|------|
| 找不到目标 | DAP 未识别 / SWD 速率过高 | 换 USB 口、降低 `adapter speed` |
| 复位后不运行 | BOOT0 跳线错误 | BOOT0 置 0（GND） |
| 烧录成功但无现象 | 外设未供电、排线没插 | 查背光、摄像头座、共地 |

### 运行

| 现象 | 定位方向 |
|------|----------|
| 屏黑但背光亮 | LTDC 引脚映射（CubeMX 自动分配常与板上走线不符）、像素时钟、层参数 |
| 画面滚动 / 撕裂 | 帧中断被自动关闭、逐行直写正在扫描的缓冲 → 三缓冲 + VBR |
| DMA 传输不启动 | 帧大小超过 16 位计数上限 → 拆成逐行 |
| 识别结果乱 | 地址宏的单位（字节 / 像素）混用；先核对强转后的实际地址 |

## 6. 已验证环境

四个工程均已在 Windows 11 + STM32CubeIDE 2.2.0（`E:\ST\STM32CubeIDE_2.2.0`）下完成完整构建，`0 errors, 0 warnings`（Debug 配置：`-O0 -g3`）：

| 工程 | 应用 | text | data | bss |
|------|------|------|------|-----|
| exp1 | led-marquee | 10 660 B | 16 B | 1 672 B |
| exp2 | timer-bench | 34 444 B | 140 B | 3 388 B |
| exp3 | home-hub | 47 980 B | 104 B | 6 584 B |
| exp4 | face-id | 79 868 B | 148 B | 53 988 B |

**内存紧张时的构建方式。** `scripts/build.sh` 走 CubeIDE headless，会拉起完整的 Eclipse 进程（峰值常驻数百 MB）。若机器内存不足、headless 被系统终止，可直接用 IDE 已生成的 makefile 构建，只需把工具链加入 `PATH`：

```bash
cd apps/<app>/firmware/Debug
make -j4 all          # make 与 arm-none-eabi-gcc 均可在 CubeIDE 的 plugins/ 下找到
```

其他平台（Linux 下 CubeIDE、macOS）未验证；脚本中的 `cygpath` 分支在非 Windows 环境会自动跳过。
