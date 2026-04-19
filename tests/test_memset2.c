void test_memset_builtin(unsigned long size) {
  char buf[100];
  __builtin_memset(buf, 0, size);
}
