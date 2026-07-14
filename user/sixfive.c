#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// 利用系统库的 strchr 函数判断字符 c 是否在官方规定的分隔符列表里
int is_sep(char c) {
  return strchr(" -\r\t\n./,", c) != 0;
}

// 逐字节读取文件并进行状态机解析
void process_file(int fd) {
  char c;
  int val = 0;
  int has_digits = 0;   // 标记当前 Token 里是否有数字
  int is_valid_num = 1; // 标记当前 Token 是否纯净（如果掺杂了字母等非分隔符，就置为 0）

  // read(fd, &c, 1) 每次从文件中读取 1 个字符
  while (read(fd, &c, 1) > 0) {
    if (is_sep(c)) {
      // 1. 如果遇到了分隔符，说明刚刚读完了一个完整的单词/Token
      // 如果它包含数字、且没有任何非法字母干扰（is_valid_num == 1）
      if (has_digits && is_valid_num) {
        // 判断是否为 5 或 6 的倍数
        if (val % 5 == 0 || val % 6 == 0) {
          printf("%d\n", val);
        }
      }
      // 遇到了分隔符后，重置所有的状态，准备迎接下一个单词
      val = 0;
      has_digits = 0;
      is_valid_num = 1;
    } else {
      // 2. 如果不是分隔符，我们来看看它是数字还是非法字符
      if (c >= '0' && c <= '9') {
        val = val * 10 + (c - '0');
        has_digits = 1;
      } else {
        // 如果遇到了像 "xv6" 里的 'x' 或 'v' 这种不是分隔符、也不是数字的字符
        // 直接宣布当前这个词无效，读到再多数字也不算数！
        is_valid_num = 0;
      }
    }
  }

  // 3. 题目提示：文件结束（EOF）也是一个隐式的分隔符！
  // 所以循环结束后，必须最后再检查一次留在缓存区里的数字
  if (has_digits && is_valid_num) {
    if (val % 5 == 0 || val % 6 == 0) {
      printf("%d\n", val);
    }
  }
}

int main(int argc, char *argv[]) {
  // 如果命令行没有指定文件名，默认从标准输入（键盘/管道，文件描述符为0）读取
  if (argc <= 1) {
    process_file(0);
  } else {
    // 遍历所有传入的文件名
    for (int i = 1; i < argc; i++) {
      int fd = open(argv[i], O_RDONLY);
      if (fd < 0) {
        fprintf(2, "sixfive: cannot open %s\n", argv[i]);
        exit(1);
      }
      process_file(fd);
      close(fd);
    }
  }
  exit(0);
}
