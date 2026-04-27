// src/irq.rs
//! 中断管理接口

use core::ffi::c_char;

/// 中断回调函数类型
pub type IrqHandler = extern "C" fn(u32);

/// 中断号枚举
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum IrqNumber {
    /// 串口0中断
    Uart0 = 33,
    /// 核心定时器中断
    Timer0 = 27,
    /// 系统定时器中断
    SystemTimer = 30,
}

/// 从原始值创建中断号
#[allow(dead_code)]
pub fn irq_number_from_raw(raw: u32) -> Option<IrqNumber> {
    match raw {
        33 => Some(IrqNumber::Uart0),
        27 => Some(IrqNumber::Timer0),
        30 => Some(IrqNumber::SystemTimer),
        _ => None,
    }
}

/// 中断注册函数
/// 
/// # 参数
/// - `irq`: 中断号
/// - `handler`: 中断处理函数
/// - `name`: 中断名称
#[inline]
pub fn register_irq(irq: u32, handler: IrqHandler, name: &str) {
    let c_name = name.as_ptr() as *const c_char;
    unsafe {
        irq_register(irq, handler, c_name);
    }
}

/// 注册中断（使用枚举）
/// 
/// # 参数
/// - `irq`: 中断号枚举
/// - `handler`: 中断处理函数
/// - `name`: 中断名称
#[inline]
pub fn register_irq_enum(irq: IrqNumber, handler: IrqHandler, name: &str) {
    register_irq(irq as u32, handler, name);
}

// 原始中断注册函数（FFI）
#[link(name = "c")]
unsafe extern "C" {
    fn irq_register(irq: u32, handler: IrqHandler, name: *const c_char);
}