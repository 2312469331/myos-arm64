// 测试函数调用时的寄存器保存

// 外部函数声明
extern void external_func(char *buf, int size);

// 测试 1：调用外部函数
void test_call_external(void) {
  char buf[100];
  buf[0] = 'A';
  buf[99] = 'Z';
  external_func(buf, 100);  // 调用外部函数
}

// 测试 2：使用 -mgeneral-regs-only 调用 memset
void test_memset_call(void) {
  char buf[100];
  buf[0] = 'A';
  buf[99] = 'Z';
  __builtin_memset(buf, 0, 100);  // 在 -mgeneral-regs-only 下会调用外部 memset
}

// 测试 3：多个函数调用
void test_multiple_calls(void) {
  char buf1[50], buf2[50];
  int x = 10, y = 20, z = 30;
  
  external_func(buf1, x);
  external_func(buf2, y);
  external_func(buf1, z);
}
