// 测试：调用外部函数前保存哪些寄存器
extern void func1(int a, int b, int c, int d, int e, int f);
extern void func2(int a, int b, int c, int d, int e, int f);

void test_caller_save(void) {
  int important1 = 100;  // 这个值在调用后还要用
  int important2 = 200;  // 这个值在调用后还要用
  int important3 = 300;  // 这个值在调用后还要用
  int important4 = 400;  // 这个值在调用后还要用
  int important5 = 500;  // 这个值在调用后还要用
  int important6 = 600;  // 这个值在调用后还要用
  int important7 = 700;  // 这个值在调用后还要用
  int important8 = 800;  // 这个值在调用后还要用
  int important9 = 900;  // 这个值在调用后还要用
  int important10 = 1000; // 这个值在调用后还要用
  
  // 第 1 次调用
  func1(important1, important2, important3, important4, 
        important5, important6);
  
  // 第 2 次调用（还要用这些值）
  func2(important7, important8, important9, important10, 
        important1, important2);
}
