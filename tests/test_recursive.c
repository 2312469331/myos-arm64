// 测试递归调用时的寄存器保存

// 递归函数
int recursive_func(int n, int value) {
  if (n <= 0) {
    return value;
  }
  
  // 递归调用后还要用 n 和 value
  int result = recursive_func(n - 1, value * 2);
  
  // 调用返回后，n 和 result 都要用
  return result + n;
}

// 测试：调用外部函数后还要用参数
void test_call_and_use(int a, int b, int c) {
  external_func(a, b);  // 调用后还要用 a, b, c
  int sum = a + b + c;
  external_func(sum, c);
}

extern void external_func(int a, int b);
