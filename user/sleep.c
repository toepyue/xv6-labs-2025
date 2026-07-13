#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  // 1. 检查命令行参数个数
  // argc 统计的是参数数量：argv[0] 是程序名 "sleep"，argv[1] 是休眠时间
  // 如果 argc 不等于 2，说明用户忘记输入时间了，立刻报错提示并退出
  if (argc != 2) {
    fprintf(2, "Usage: sleep <ticks>\n");
    exit(1); // 传入非 0 值表示异常退出
  }

  // 2. 将字符串参数转换为整数
  // 命令行读进来的 "10" 是文本字符串，必须用 xv6 自带的 atoi 函数转成整型数字
  int ticks = atoi(argv[1]);

  // 3. 调用 2025 版专属的内核系统调用 pause()
  // 暂停当前进程指定的时钟滴答数
  pause(ticks);

  // 4. 正常执行结束，退出当前进程
 
 exit(0); // 传入 0 表示成功完成
}
