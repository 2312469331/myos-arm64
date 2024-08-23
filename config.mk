# ==============================================
# 裸机 OS 功能配置开关（条件编译核心）
# ==============================================

# 1. 核心功能开关（y=启用，n=禁用）
CONFIG_EXCEPTION ?= y# 异常向量表 + 同步异常处理
CONFIG_GIC       ?= y# GICv2中断控制器
CONFIG_IRQ       ?= y# 外设中断注册（依赖CONFIG_GIC）
CONFIG_UART	 ?= y# 外设中断注册（依赖CONFIG_GIC）

$(info CONFIG_EXCEPTION = [$(CONFIG_EXCEPTION)])
# 2. 条件加入源码（根据配置自动生成 SRC_ASM/SRC_C）
# 异常处理相关
ifeq ($(CONFIG_EXCEPTION),y)
SRC_ASM_CONFIG += boot/vector.S          # 异常向量表
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



