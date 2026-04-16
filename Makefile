# 引入功能配置（条件编译）
include config.mk
$(info SRC_ASM_CONFIG = $(SRC_ASM_CONFIG))

# --------------- QEMU 全局公共配置 ---------------

# qemu配置

# 编译器/工具配置（沿用你现有配置）
QEMU := qemu-system-aarch64
QEMU_MACHINE := virt,secure=on,virtualization=on
QEMU_CPU := cortex-a53
DTC := dtc
QEMU_MEM		= 256M

# 公共基础参数(所有目标共用)
QEMU_BASE_ARGS	:= \
	-machine $(QEMU_MACHINE) \
	-cpu $(QEMU_CPU) \
	-m $(QEMU_MEM)

# ---------------------------------------------------
# 架构变量，可通过命令行指定：make ARCH=arm64 或 make ARCH=x86_64
 ARCH ?= arm64
 # 项目根目录绝对路径（避免相对路径问题）
 ROOT_DIR := $(CURDIR)
 # 1. 动态头文件搜索路径（-I）
 # 通用头文件 + 架构专属头文件
 INCLUDES := -I$(ROOT_DIR)/include
 INCLUDES += -I$(ROOT_DIR)/arch/$(ARCH)/include
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
          -fno-PIC -fno-PIE \
          -g
# 3. 最终编译/链接选项
CFLAGS += $(INCLUDES) 
ASFLAGS := -Iinclude \
           --target=aarch64-elf -mcpu=cortex-a53 -march=armv8-a \
           -g

# ============================================================
# 链接器选项：彻底排除系统 CRT、强制入口 _start
# ============================================================
LDFLAGS := -fuse-ld=lld \
           -T arch/arm64/boot/link.ld \
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
BOOT_CFLAGS += -fno-stack-protector -O0 -Wall -g
BOOT_CFLAGS += -Iarch/arm64/include
# 3. 最终编译/链接选项
BOOT_CFLAGS+= $(INCLUDES)
else ifeq ($(OS),Windows_NT)
    # --- x86_64 Windows 主机环境配置 ---
    CROSS_COMPILE := aarch64-none-elf-
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
               $(INCLUDES) \
                -fno-PIC -fno-PIE \
    	       -g
    ASFLAGS := $(INCLUDES) -g
    BOOT_CFLAGS := -march=armv8-a -mgeneral-regs-only -ffreestanding
    BOOT_CFLAGS += -nostdlib -fno-builtin -fno-PIC -fno-PIE
    BOOT_CFLAGS += -fno-stack-protector -O0 -Wall -g
    BOOT_CFLAGS += $(INCLUDES)
    LDFLAGS := -T arch/$(ARCH)/boot/link.ld \
               -nostdlib
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
    ASFLAGS := -Iinclude -g
    BOOT_CFLAGS := -march=armv8-a -mgeneral-regs-only -ffreestanding
    BOOT_CFLAGS += -nostdlib -fno-builtin -fno-PIC -fno-PIE
    BOOT_CFLAGS += -fno-stack-protector -O0 -Wall -g
    BOOT_CFLAGS += -Iinclude
    LDFLAGS := -T arch/arm64/boot/link.ld \
               -nostdlib
endif
# =========================
 # 源码配置（合并 config.mk 配置）
 # =========================
 SRC_ASM = arch/arm64/boot/boot.S \
           $(SRC_ASM_CONFIG)   # 来自 config.mk 的条件汇编文件
 SRC_C = arch/arm64/boot/bootc.c \
         kernel/main.c \
         kernel/irq.c \
         kernel/pmm.c \
         kernel/printk.c \
         kernel/libc.c \
         kernel/slab.c \
         kernel/mmu.c \
         $(SRC_C_CONFIG)       # 来自 config.mk 的条件C文件

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
	$(if $(findstring arch/arm64/boot/,$<),$(CC) $(BOOT_CFLAGS) -c $< -o $@,$(CC) $(CFLAGS) -c $< -o $@)
# 链接 ELF（仅链接 .o 文件，无源码！）
$(TARGET).elf: $(OBJ)
	$(LD) $(LDFLAGS) $^ -o $@

# 生成 IMG 镜像
$(TARGET).img: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# ======================
# 你要的：IMG 启动 QEMU
# ======================

# 正常运行
run: all
	$(QEMU) $(QEMU_BASE_ARGS) \
		-kernel $(TARGET).elf \
		-nographic

# 串口独立调试
serial: all
	$(QEMU) $(QEMU_BASE_ARGS) \
		-kernel $(TARGET).img \
		-serial pty -daemonize -display none

# GDB 阻塞等待调试
debug: all
	$(QEMU) $(QEMU_BASE_ARGS) \
		-kernel $(TARGET).elf \
		-nographic -s -S

# GDB 不阻塞直接运行
debug-no-suspend: all
	$(QEMU) $(QEMU_BASE_ARGS) \
		-kernel $(TARGET).elf \
		-nographic -s

# 导出设备树
.PHONY: dtb
dtb:
	@echo "📤 Exporting QEMU device tree (DTB)..."
	$(QEMU) \
		-machine $(QEMU_MACHINE),dumpdtb=virt.dtb \
		-cpu $(QEMU_CPU) \
		-m $(QEMU_MEM) \
		-nographic
	@echo "🔄 Converting DTB to DTS..."
	$(DTC) -I dtb -O dts -o virt.dts virt.dtb
	@echo "✅ Done: virt.dtb (binary) + virt.dts (source) generated"

.PHONY: debug debug-no-suspend dtb clean-dtb clean
clean-dtb:
	rm -f virt.dtb virt.dts

clean:
	rm -rf build/*

