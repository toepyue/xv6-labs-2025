// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages
// and 2-megabyte superpages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// 1. 普通 4KB 小页内存池
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// 2. 专属 2MB 超级大页内存池
struct {
  struct spinlock lock;
  struct run *freelist;
} superkmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&superkmem.lock, "superkmem");

  // 核心分池策略：从物理内存最高点 PHYSTOP 向下切出 32MB 作为 2MB 大页专属区
  // 32MB 可以提供 16 个 2MB 超级大页，完美满足 MIT "a handful of chunks" 的实验要求
  void *super_start = (void*)((uint64)PHYSTOP - 32 * 1024 * 1024);

  // 普通 4KB 小页：从内核结束位置 (end) 一直分配到大页区的起始边界
  freerange(end, super_start);

  // 超级大页：从 super_start 一直切到 PHYSTOP，严格以 2MB (0x200000) 为步长挂入专属链表
  char *p = (char*)super_start;
  for(; p + (2 * 1024 * 1024) <= (char*)PHYSTOP; p += (2 * 1024 * 1024)){
    superfree(p);
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// ----------------------------------------------------------------
// 2MB 超级大页 (Superpage) 专属分配与释放接口
// ----------------------------------------------------------------

// 释放一个 2MB 的物理超级大页到 superkmem 链表中
void
superfree(void *pa)
{
  struct run *r;

  // 严格的地址边界与 2MB 对齐校验 (2MB = 2 * 1024 * 1024 = 0x200000)
  if(((uint64)pa % (2 * 1024 * 1024)) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("superfree");

  // 填入垃圾数据，防范悬挂指针和未初始化读写
  memset(pa, 1, 2 * 1024 * 1024);

  r = (struct run*)pa;

  acquire(&superkmem.lock);
  r->next = superkmem.freelist;
  superkmem.freelist = r;
  release(&superkmem.lock);
}

// O(1) 极速申请一个 2MB 连续且对齐的物理大页
void *
superalloc(void)
{
  struct run *r;

  acquire(&superkmem.lock);
  r = superkmem.freelist;
  if(r)
    superkmem.freelist = r->next;
  release(&superkmem.lock);

  if(r)
    memset((char*)r, 5, 2 * 1024 * 1024); // fill with junk
  return (void*)r;
}