// 测试递归调用时的寄存器保存机制
// 编译：gcc -O0 -S test_recursive_detailed.c -o test_recursive_detailed.s

int recursive_func(int n, int value) {
    if (n <= 0) {
        return value;
    }
    
    // 在调用 recursive_func(n-1, value*2) 之前：
    // 1. n 和 value 还需要在调用返回后使用（return result + n）
    // 2. 但 x0-x7 是 caller-saved，会被调用函数修改
    // 编译器会怎么做？
    
    int result = recursive_func(n - 1, value * 2);
    
    // 调用返回后：
    // - result 在 x0 中（返回值寄存器）
    // - n 需要从栈上恢复
    // 然后计算 result + n
    
    return result + n;
}

// 更复杂的例子：多个参数在调用后还要用
int complex_recursive(int a, int b, int c, int depth) {
    if (depth <= 0) {
        return a + b + c;
    }
    
    // 调用前：a, b, c, depth 都要在返回后使用
    // 但 x0-x7 只有 8 个，传参就用掉 4 个
    // 调用 complex_recursive 会修改 x0-x7
    
    int temp = complex_recursive(a - 1, b, c, depth - 1);
    
    // 返回后：
    // - temp 在 x0
    // - a, b, c, depth 需要从栈上恢复
    // 然后还要调用一次
    int temp2 = complex_recursive(a, b - 1, c, depth - 1);
    
    // 再次返回后，还要用 a, b, c
    return temp + temp2 + a + b + c;
}

// 测试：调用外部函数
extern void external_func(int a, int b, int c, int d, int e, int f);

void test_external_call(int a, int b, int c) {
    // 调用 external_func 前
    // external_func 会修改 x0-x7（参数寄存器）和 x9-x15（调用破坏寄存器）
    // 但调用后还要用 a, b, c
    
    external_func(a, b, c, 1, 2, 3);
    
    // 返回后 a, b, c 必须恢复
    int sum = a + b + c;
    
    external_func(sum, a, b, c, 1, 2);
}
