// 测试 NEON 寄存器使用情况
extern void external_func(char *buf, int size);

// 测试：使用 memset（可能会用 NEON 优化）
void test_with_neon(void) {
  char buf[256];  // 大一点的缓冲区，可能触发 NEON 优化
  __builtin_memset(buf, 0, 256);
}

// 测试：多次调用，中间可能有 NEON 数据需要保存
void test_multi_with_neon(void) {
  char buf1[256], buf2[256];
  __builtin_memset(buf1, 'A', 256);
  external_func(buf1, 256);
  __builtin_memset(buf2, 'B', 256);
  external_func(buf2, 256);
}
