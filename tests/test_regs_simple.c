// 测试函数调用时的寄存器保存（无栈保护）
extern void external_func(char *buf, int size);

// 测试 1：简单调用
void test_simple_call(void) {
  char buf[100];
  buf[0] = 'A';
  buf[99] = 'Z';
  external_func(buf, 100);
}

// 测试 2：调用 memset
void test_memset_simple(void) {
  char buf[100];
  __builtin_memset(buf, 0, 100);
}

// 测试 3：多次调用
void test_multi_call(void) {
  char buf1[50], buf2[50];
  int x = 10, y = 20, z = 30;
  
  external_func(buf1, x);
  external_func(buf2, y);
  external_func(buf1, z);
}
