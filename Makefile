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

    # 裸机编译选项（适配Clang）
    CFLAGS  := -Wall -Wextra \
               -ffreestanding \
               -nostdlib \
               -fno-stack-protector \
               -Iinclude
#    LDFLAGS := -fuse-ld=lld -T boot/link.ld -nostdlib
    LDFLAGS := -fuse-ld=lld -T boot/link.ld -nostdlib

else
    # --- 主机 (x86_64) 环境配置 ---
    CROSS_COMPILE := aarch64-linux-gnu-
    CC      := $(CROSS_COMPILE)gcc
    AS      := $(CROSS_COMPILE)as
    LD      := $(CROSS_COMPILE)ld
    OBJCOPY := $(CROSS_COMPILE)objcopy
    OBJDUMP := $(CROSS_COMPILE)objdump
    QEMU    := qemu-system-aarch64

    # 裸机编译选项（适配GCC）
    CFLAGS  := -Wall -Wextra \
               -ffreestanding \
               -nostdlib \
               -nostartfiles \
               -fno-stack-protector \
               -Iinclude
    LDFLAGS := -T boot/link.ld
endif

# 源码列表（新增/删除文件只需改这里）
SRC_ASM = boot/boot.S
SRC_C   = kernel/main.c \
          kernel/uart.c \
          kernel/mmu.c \
          kernel/irq.c

# 目标文件和输出镜像
OBJ     = $(patsubst %.S,build/%.o,$(SRC_ASM)) \
          $(patsubst %.c,build/%.o,$(SRC_C))
TARGET  = build/kernel

# 默认目标：编译内核镜像
all: build_dir $(TARGET).img

# 创建build目录（自动生成）
build_dir:
	mkdir -p build/boot build/kernel

# 汇编文件编译规则
build/%.o: %.S
	$(AS) $(CFLAGS) -c $< -o $@

# C文件编译规则
build/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 链接生成ELF
$(TARGET).elf: $(OBJ)
	$(LD) $(LDFLAGS) $^ -o $@

# 转成二进制镜像
$(TARGET).img: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# 一键运行：编译 + 启动QEMU
#run: all
#	$(QEMU) -machine virt -cpu cortex-a53 \
	-kernel $(TARGET).elf -nographic  # 把 .img 改成 .elf
# 一键运行
run: all
	$(QEMU) -machine virt -cpu cortex-a53  \
 		-kernel $(TARGET).img -nographic
# 一键调试：编译 + 启动QEMU并等待GDB
debug: all
	$(QEMU) -machine virt -cpu cortex-a53 -m 128M \
		-kernel $(TARGET).img -nographic -s -S

# 清理所有编译产物
clean:
	rm -rf build/*

