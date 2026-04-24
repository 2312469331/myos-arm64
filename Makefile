# 引入功能配置（条件编译）
include config.mk
$(info SRC_ASM_CONFIG = $(SRC_ASM_CONFIG))

# --------------- QEMU 全局公共配置 ---------------

# qemu配置

# 编译器/工具配置（沿用现有配置）
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
 ARCH = arm64
 # 项目根目录绝对路径（避免相对路径问题）
 ROOT_DIR := $(CURDIR)
 # 1. 动态头文件搜索路径（-I）
# 通用头文件 + 架构专属头文件
INCLUDES := -I$(ROOT_DIR)/arch/$(ARCH)/include
INCLUDES += -I$(ROOT_DIR)/include
INCLUDES += -I$(ROOT_DIR)

# ARM64 架构特定头文件
ifeq ($(ARCH),arm64)
INCLUDES += -I$(ROOT_DIR)/arch/$(ARCH)/Core/Include
INCLUDES += -I$(ROOT_DIR)/arch/$(ARCH)/Core/Include/a-profile
INCLUDES += -I$(ROOT_DIR)/arch/$(ARCH)/device/ARMCA53/Include
endif

# ============================================================
# 通用编译选项：所有环境共用
# ============================================================
# 核心编译选项：彻底关闭 PIC/PIE、GOT、CRT
CFLAGS := -Wall -Wextra \
          -O0 \
          -ffreestanding \
          -nostdlib \
          -fno-stack-protector \
          -mgeneral-regs-only \
          -fno-PIC -fno-PIE \
          -g
# 追加包含路径
CFLAGS += $(INCLUDES)

# 汇编选项
ASFLAGS := $(INCLUDES) -g

# Boot 段编译配置（已彻底关闭 -fPIC）
BOOT_CFLAGS := -march=armv8-a -mgeneral-regs-only -ffreestanding
BOOT_CFLAGS += -nostdlib -fno-builtin -fno-PIC -fno-PIE
BOOT_CFLAGS += -fno-stack-protector -O0 -Wall -g
BOOT_CFLAGS += $(INCLUDES)

# 链接器选项：彻底排除系统 CRT、强制入口 _start
LDFLAGS := -T arch/arm64/boot/link.ld \
           -nostdlib \
           -static \
           -e _start

# ARM64 架构特定编译选项
ifeq ($(ARCH),arm64)
# RTE 宏定义
CFLAGS += -D_RTE_
# ARM64 特定优化
CFLAGS += -mcpu=cortex-a53 -march=armv8-a
ASFLAGS += -mcpu=cortex-a53 -march=armv8-a
endif

# ============================================================
# 环境特定配置
# ============================================================
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
    # Termux 特定链接器选项
    LDFLAGS += -fuse-ld=lld \
               -Wl,--build-id=none \
               -Wl,--no-dynamic-linker
else ifeq ($(OS),Windows_NT)
    # --- x86_64 Windows 主机环境配置 ---
    CROSS_COMPILE := aarch64-none-elf-
    CC      := $(CROSS_COMPILE)gcc
    AS      := $(CROSS_COMPILE)as
    LD      := $(CROSS_COMPILE)ld
    OBJCOPY := $(CROSS_COMPILE)objcopy
    OBJDUMP := $(CROSS_COMPILE)objdump
    QEMU    := qemu-system-aarch64
else
    # --- x86_64 Ubuntu 主机环境配置 ---
    CROSS_COMPILE := aarch64-linux-gnu-
    CC      := $(CROSS_COMPILE)gcc
    AS      := $(CROSS_COMPILE)as
    LD      := $(CROSS_COMPILE)ld
    OBJCOPY := $(CROSS_COMPILE)objcopy
    OBJDUMP := $(CROSS_COMPILE)objdump
    QEMU    := qemu-system-aarch64
endif
# =========================
 # 源码配置（合并 config.mk 配置）
 # =========================
 SRC_ASM = arch/arm64/boot/boot.S \
           $(SRC_ASM_CONFIG)   # 来自 config.mk 的条件汇编文件
 SRC_C = kernel/main.c \
                kernel/irq.c \
                kernel/pmm.c \
                kernel/printk.c \
                kernel/libc.c \
                kernel/slab.c \
                kernel/mmu.c \
                kernel/pgtbl.c \
                kernel/vmalloc.c \
                kernel/vmap.c \
                kernel/page_fault.c \
                kernel/ds/rbtree.c \
                kernel/sync/completion.c \
                kernel/sync/rcupdate.c \
                kernel/sync/rwsem.c \
                kernel/sync/wait.c \
                kernel/sync/semaphore.c \
                $(SRC_C_CONFIG)       # 来自 config.mk 的条件C文件

# ARM64 架构特定源文件
ifeq ($(ARCH),arm64)
SRC_C += arch/arm64/boot/bootc.c \
         arch/arm64/Core/Source/irq_ctrl_gic.c
SRC_C += arch/arm64/device/ARMCA53/Source/startup_ARMCA53.c
SRC_C += arch/arm64/device/ARMCA53/Source/system_ARMCA53.c
endif

# 🚨【核心修复】仅生成编译后的 .o 文件，绝不混入源码！
OBJ     = $(patsubst %.S,build/%.o,$(SRC_ASM)) \
          $(patsubst %.c,build/%.o,$(SRC_C))
# 👇 新增：Rust 静态库
RUST_LIB = rust/wrapper/target/aarch64-unknown-none/debug/libmyos_wrapper.a
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
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

# C 文件编译
build/%.o: %.c
	@mkdir -p $(dir $@)
	$(if $(findstring arch/arm64/boot/,$<),$(CC) $(BOOT_CFLAGS) -c $< -o $@,$(CC) $(CFLAGS) -c $< -o $@)
# 链接 ELF（仅链接 .o 文件，无源码！）
$(TARGET).elf: $(OBJ) $(RUST_LIB)
	$(LD) $(LDFLAGS) $^ -o $@

# 生成 IMG 镜像
$(TARGET).img: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# ======================
# IMG 启动 QEMU
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

# 获取设备树起始地址
DTB_ADDR = 0x40000000

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
	@echo "📍 DTB will be loaded at address: $(DTB_ADDR)"

.PHONY: debug debug-no-suspend dtb clean-dtb clean readelf readelf-l readelf-S readelf-s
clean-dtb:
	rm -f virt.dtb virt.dts

clean:
	rm -rf build/*

# 查看 ELF 文件段信息 (-l)
readelf-l:
	$(CROSS_COMPILE)readelf -l $(TARGET).elf

# 查看 ELF 文件节信息 (-S)
readelf-S:
	$(CROSS_COMPILE)readelf -S $(TARGET).elf

# 查看 ELF 文件符号表 (-s)
readelf-s:
	$(CROSS_COMPILE)readelf -s $(TARGET).elf

# 查看 ELF 文件所有信息
readelf:
	@echo "📋 查看 ELF 文件段信息 (-l)"
	@$(MAKE) readelf-l
	@echo "\n📋 查看 ELF 文件节信息 (-S)"
	@$(MAKE) readelf-S
	@echo "\n📋 查看 ELF 文件符号表 (-s)"
	@$(MAKE) readelf-s

