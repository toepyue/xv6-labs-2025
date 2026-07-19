#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  backtrace();
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 1. 用户调用 sigalarm(ticks, handler) 开启或关闭闹钟
uint64
sys_sigalarm(void)
{
  int ticks;
  uint64 handler;
  struct proc *p = myproc();

  // 从用户态把参数抓进内核
  argint(0, &ticks);
  argaddr(1, &handler);

  p->alarm_interval = ticks;
  p->alarm_handler = (void (*)())handler;
  p->alarm_ticks = 0;
  p->alarm_executing = 0;
  return 0;
}

// 2. 用户处理函数执行完后，调 sigreturn() 恢复中断前的原状
uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();

  //  核心：把备份在 alarm_tf 里的所有寄存器原封不动拷回 p->trapframe
  // 这会直接覆盖掉回调过程中对寄存器的各种改变，包含原先打断时的 epc 地址。
  *p->trapframe = *p->alarm_tf;

  // 解开防重入锁，重新开始计算下一次中断周期
  p->alarm_executing = 0;

  return p->trapframe->a0;
}
