# 工具链配置（适配Termux的aarch64-linux-android-clang）
CC      = aarch64-linux-android-clang
AS      = aarch64-linux-android-clang
LD      = aarch64-linux-android-ld
OBJCOPY = aarch64-linux-android-objcopy
OBJDUMP = aarch64-linux-android-objdump
QEMU    = qemu-system-aarch64

# 裸机编译选项（必须加，脱离Android环境）
CFLAGS  = -Wall -Wextra \
          -ffreestanding \
          -nostdlib \
          -nostartfiles \
          -fno-stack-protector \
          -Iinclude
LDFLAGS = -T boot/link.ld

# 源码列表（新增文件只需要改这里）
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

# 转成QEMU可加载的二进制镜像
$(TARGET).img: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# 一键运行：编译 + 启动QEMU
run: all
	$(QEMU) -machine virt -cpu cortex-a53 -m 128M \
		-kernel $(TARGET).img -nographic -serial mon:stdio

# 一键调试：编译 + 启动QEMU并等待GDB连接
debug: all
	$(QEMU) -machine virt -cpu cortex-a53 -m 128M \
		-kernel $(TARGET).img -nographic -s -S

# 清理所有编译产物
clean:
	rm -rf build/*

