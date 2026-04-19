// 测试文件：验证 __builtin_* 函数的行为
// 这个文件单独编译，不使用 -nostdlib

// 不调用任何头文件，直接调用 __builtin_* 函数

// 测试 1：直接调用 __builtin_memcpy，不提供实现
void test_memcpy(void) {
  char src[] = "hello";
  char dst[10];
  __builtin_memcpy(dst, src, 6);  // 直接调用 builtin
}

// 测试 2：直接调用 __builtin_strlen，不提供实现
void test_strlen(void) {
  char *str = "test";
  unsigned long len = __builtin_strlen(str);  // 直接调用 builtin
}

// 测试 3：直接调用 __builtin_memset，不提供实现
void test_memset(void) {
  char buffer[100];
  __builtin_memset(buffer, 0, 100);  // 直接调用 builtin
}

int main(void) {
  test_memcpy();
  test_strlen();
  test_memset();
  return 0;
}
