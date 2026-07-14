#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data);

int
main(int argc, char *argv[])
{
  if(argc == 1){
    printf("Example 1:\n");
    int a[2] = { 61810, 2025 };
    memdump("ii", (char*) a);
    
    printf("Example 2:\n");
    memdump("S", "a string");
    
    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *) &s);

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;
    
    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");
    
    printf("Example 4:\n");
    memdump("pihcS", (char*) &example);
    
    printf("Example 5:\n");
    memdump("sccccc", (char*) &example);
  } else if(argc == 2){
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while(n < sizeof(data)){
      int nn = read(0, data + n, sizeof(data) - n);
      if(nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data)
{
  // Your code here.
// 遍历格式化字符串中的每一个字符
  for (int idx = 0; fmt[idx] != '\0'; idx++) {
    char ch = fmt[idx];
    
    if (ch == 'i') {
      // 4字节 32位十进制整数
      printf("%d\n", *(int *)data);
      data += 4;
    } else if (ch == 'p') {
      // 8字节 64位十六进制整数（注意：用 %lx 匹配示例中的无 0x 前缀输出）
      printf("%lx\n", *(unsigned long *)data);
      data += 8;
    } else if (ch == 'h') {
      // 2字节 16位十进制短整数
      printf("%d\n", *(short *)data);
      data += 2;
    } else if (ch == 'c') {
      // 1字节 8位 ASCII 字符
      printf("%c\n", *data);
      data += 1;
    } else if (ch == 's') {
      // 8字节：这里存放的是一个“指向C字符串的64位指针”
      printf("%s\n", *(char **)data);
      data += 8;
    } else if (ch == 'S') {
      // 剩余的所有字节：本身就是一个以 \0 结尾的C字符串
      printf("%s\n", data);
      break; // 打印完直接退出循环，不再解析后续格式
    }
  }

}
