.text
.global _user_start

// 用户态入口
// 系统调用号: X8
//   0 = print(X1=addr, X2=len)
//   1 = exit()
_user_start:
    // 打印 "Hello from EL0!\n"
    MOV     X8, #0              // syscall号: print
    ADR     X1, msg             // 消息地址
    MOV     X2, #18             // 消息长度
    SVC     #0                  // 触发系统调用

    // 退出
    MOV     X8, #1              // syscall号: exit
    SVC     #0

    // 不会到这里
    B       .

msg:
    .ascii  "Hello from EL0!\n"
