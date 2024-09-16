# ==============================================
# 裸机 OS 功能配置开关（条件编译核心）
# ==============================================

# 1. 核心功能开关（y=启用，n=禁用）
CONFIG_EXCEPTION ?= y# 异常向量表 + 同步异常处理
CONFIG_GIC       ?= y# GICv2中断控制器
CONFIG_IRQ       ?= y# 外设中断注册（依赖CONFIG_GIC）
CONFIG_UART	 ?= y# 外设中断注册（依赖CONFIG_GIC）
CONFIG_FDT 	 ?= n# 设备树解析（libfdt 裸机版）
CONFIG_TIMER  ?= y
$(info CONFIG_EXCEPTION = [$(CONFIG_EXCEPTION)])
# 2. 条件加入源码（根据配置自动生成 SRC_ASM/SRC_C）
# 异常处理相关
ifeq ($(CONFIG_EXCEPTION),y)
SRC_ASM_CONFIG += arch/arm64/boot/vector.S          # 异常向量表
SRC_C_CONFIG   += exception/handler.c    # 异常/中断处理函数
endif

# GIC 中断相关（CONFIG_IRQ 依赖 CONFIG_GIC）
ifeq ($(CONFIG_GIC),y)
SRC_C_CONFIG   += driver/gic.c            # GICv2驱动
ifeq ($(CONFIG_IRQ),y)
# 额外：CONFIG_IRQ 依赖 CONFIG_GIC，这里可以加更多中断相关文件
endif
endif

# UART 相关（CONFIG_UART 依赖 CONFIG_UART）
ifeq ($(CONFIG_UART),y)
SRC_C_CONFIG   += driver/uart.c            # GICv2驱动
endif

ifeq ($(CONFIG_TIMER),y)
# 若你有定时器底层汇编文件（比如 timer_asm.S），才需要这一行
# SRC_ASM_CONFIG += timer/timer_asm.S  # 定时器底层汇编实现
SRC_C_CONFIG   += driver/timer.c        # Cortex-A53定时器驱动（进程调度基准时钟）
endif

# FDT 设备树解析相关
ifeq ($(CONFIG_FDT),y)
SRC_C_CONFIG += driver/fdt/fdt.c          # libfdt 核心源码
SRC_C_CONFIG += driver/fdt/fdt_ro.c       # 只读设备树操作
SRC_C_CONFIG += driver/fdt/fdt_wip.c      #  wip 功能（可选）
SRC_C_CONFIG += driver/fdt/fdt_sw.c       #  设备树写操作（可选）
SRC_C_CONFIG += driver/fdt/fdt_rw.c       #  读写设备树
SRC_C_CONFIG += driver/fdt/fdt_strerror.c # 错误信息处理
SRC_C_CONFIG += driver/fdt/fdt_empty_tree.c # 空设备树（可选）
SRC_C_CONFIG += driver/fdt/fdt_addresses.c # 地址解析（可选）
endif

