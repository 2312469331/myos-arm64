void test_memset_builtin(char *buf, unsigned long size) {
  __builtin_memset(buf, 0, size);
}
