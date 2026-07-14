#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h" // 引入 MAXARG 定义

// 全局变量：保存 -exec 后面的命令和参数
int has_exec = 0;
char *exec_cmd = 0;
char *exec_argv[MAXARG];
int exec_argc = 0;

// 辅助函数：从长路径 "a/b/c" 中提取出最后的具体文件名 "c"
char* fmtname(char *path)
{
  char *p;
  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}

// 核心递归查找函数
void find(char *path, char *target)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    // 如果找到了目标文件名
    if(strcmp(fmtname(path), target) == 0){
      if(has_exec){
        // 1. 如果带有 -exec 参数，执行 fork + exec
        int pid = fork();
        if(pid < 0){
          fprintf(2, "find: fork failed\n");
        } else if(pid == 0){
          // 子进程：组装命令参数数组
          char *cmd_args[MAXARG];
          for(int i = 0; i < exec_argc; i++){
            cmd_args[i] = exec_argv[i];
          }
          // 把当前找到的文件路径，拼在参数数组的最后面
          cmd_args[exec_argc] = path;
          cmd_args[exec_argc + 1] = 0; // 参数数组必须以 NULL 结尾！

          // 调用系统执行程序
          exec(exec_cmd, cmd_args);
          // 如果 exec 返回了，说明执行失败
          fprintf(2, "find: exec %s failed\n", exec_cmd);
          exit(1);
        } else {
          // 父进程：等待子进程执行结束
          wait(0);
        }
      } else {
        // 2. 如果没带 -exec，依然按原样直接打印路径
        printf("%s\n", path);
      }
    }
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
      fprintf(2, "find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      if(stat(buf, &st) < 0){
        printf("find: cannot stat %s\n", buf);
        continue;
      }
      find(buf, target);
    }
    break;
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "Usage: find <path> <filename> [-exec cmd ...]\n");
    exit(1);
  }

  // 解析是否包含 -exec 参数
  if(argc >= 5 && strcmp(argv[3], "-exec") == 0){
    has_exec = 1;
    exec_cmd = argv[4]; // 后面的第一个词就是真正要跑的程序（如 echo）
    for(int i = 4; i < argc; i++){
      exec_argv[exec_argc++] = argv[i];
    }
  }

  find(argv[1], argv[2]);
  exit(0);
}
