# 引入功能配置（条件编译）
include config.mk
$(info SRC_ASM_CONFIG = $(SRC_ASM_CONFIG))

# 检测是否在Termux环境中
ifeq ($(shell uname -o), Android)
    # --- Termux 环境配置 ---
    CROSS_COMPILE := aarch64-linux-android-
    CC      := $(CROSS_COMPILE)clang
    AS      := $(CROSS_COMPILE)clang
    LD      := $(CROSS_COMPILE)clang
    OBJCOPY := llvm-objcopy
    OBJDUMP := llvm-objdump
    QEMU    := qemu-system-aarch64

# ============================================================
# 核心编译选项：彻底关闭 PIC/PIE、GOT、CRT
# ============================================================
CFLAGS := -Wall -Wextra \
          -O0 \
          --target=aarch64-elf -mcpu=cortex-a53 -march=armv8-a \
          -ffreestanding \
          -nostdlib \
          -static \
          -fno-stack-protector \
          -mgeneral-regs-only \
          -Iinclude \
          -fno-PIC -fno-PIE \
          -g

ASFLAGS := -Iinclude \
           --target=aarch64-elf -mcpu=cortex-a53 -march=armv8-a \
           -g

# ============================================================
# 链接器选项：彻底排除系统 CRT、强制入口 _start
# ============================================================
LDFLAGS := -fuse-ld=lld \
           -T boot/link.ld \
           -nostdlib \
           -nodefaultlibs \
           -static \
           -e _start \
           -Wl,--build-id=none \
           -Wl,--no-dynamic-linker

# ============================================================
# Boot 段编译配置（已彻底关闭 -fPIC）
# ============================================================
BOOT_CFLAGS := -march=armv8-a -mgeneral-regs-only -ffreestanding
BOOT_CFLAGS += -nostdlib -fno-builtin -fno-PIC -fno-PIE
BOOT_CFLAGS += -fno-stack-protector -O2 -Wall -g

else
    # --- x86_64 Ubuntu 主机环境配置 ---
    CROSS_COMPILE := aarch64-linux-gnu-
    CC      := $(CROSS_COMPILE)gcc
    AS      := $(CROSS_COMPILE)as
    LD      := $(CROSS_COMPILE)ld
    OBJCOPY := $(CROSS_COMPILE)objcopy
    OBJDUMP := $(CROSS_COMPILE)objdump
    QEMU    := qemu-system-aarch64

    CFLAGS  := -Wall -Wextra \
               -ffreestanding \
               -nostdlib \
               -nostartfiles \
               -fno-stack-protector \
               -mgeneral-regs-only \
               -Iinclude \
    	       -g
    ASFLAGS := -Iinclude
    LDFLAGS := -T boot/link.ld \
               -nostdlib \
               -nostartfiles
endif
# qemu配置

# 编译器/工具配置（沿用你现有配置）
QEMU := qemu-system-aarch64
QEMU_MACHINE := virt,secure=on,virtualization=on
QEMU_CPU := cortex-a53
DTC := dtc
# ======================
# 源码配置（合并 config.mk 配置）
# ======================
SRC_ASM = boot/boot.S \
           $(SRC_ASM_CONFIG)  # 来自 config.mk 的条件汇编文件
SRC_C = boot/bootc.c \
           kernel/main.c \
           kernel/mmu.c \
           kernel/irq.c \
           kernel/printk.c \
           $(SRC_C_CONFIG)    # 来自 config.mk 的条件C文件


# 🚨【核心修复】仅生成编译后的 .o 文件，绝不混入源码！
OBJ     = $(patsubst %.S,build/%.o,$(SRC_ASM)) \
          $(patsubst %.c,build/%.o,$(SRC_C))
# 👇 新增：提取所有 .o 文件的目录，并去重
OBJ_DIRS := $(sort $(dir $(OBJ)))
TARGET  = build/kernel

# 默认编译
# all: build_dir $(TARGET).img

all: build_dir $(TARGET).elf
# 创建编译目录

# 👇 修改 build_dir 目标，自动创建所有需要的目录
build_dir:
	mkdir -p $(OBJ_DIRS)

# 汇编文件编译
build/%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

# C 文件编译
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(if $(findstring boot/,$<),$(CC) $(BOOT_CFLAGS) -c $< -o $@,$(CC) $(CFLAGS) -c $< -o $@)
# 链接 ELF（仅链接 .o 文件，无源码！）
$(TARGET).elf: $(OBJ)
	$(LD) $(LDFLAGS) $^ -o $@

# 生成 IMG 镜像
$(TARGET).img: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# ======================
# 你要的：IMG 启动 QEMU
# ======================
run: all
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m 128M \
		-kernel $(TARGET).elf -nographic

# 🔥 新增：串口独立调试模式（推荐！）
serial: all
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m 128M \
		-kernel $(TARGET).img -serial pty -daemonize -display none

debug: all
	$(QEMU) -machine $(QEMU_MACHINE) -cpu $(QEMU_CPU) -m 128M \
		-kernel $(TARGET).elf -nographic -s -S

# 导出设备树（DTB + DTS）
.PHONY: dtb
dtb:
	@echo "📤 Exporting QEMU device tree (DTB)..."
	$(QEMU) -machine $(QEMU_MACHINE),dumpdtb=virt.dtb \
		-cpu $(QEMU_CPU) \
		-m 128M \
		-nographic
	@echo "🔄 Converting DTB to DTS..."
	$(DTC) -I dtb -O dts -o virt.dts virt.dtb
	@echo "✅ Done: virt.dtb (binary) + virt.dts (source) generated"

# 清理设备树文件
.PHONY: clean-dtb
clean-dtb:
	rm -f virt.dtb virt.dts

clean:
	rm -rf build/*
