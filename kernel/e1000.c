#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
// 存放指向发送缓冲区的指针，以便在网卡发送完毕后调用 kfree() 释放内存
static char *tx_bufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
// this code loosely follows the initialization directions
// in Chapter 14 of Intel's Software Developer's Manual.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_ring[i].addr = 0;
    tx_bufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_ring[i].addr = (uint64) kalloc();
    if (!rx_ring[i].addr)
      panic("e1000");
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(char *buf, int len)
{
  acquire(&e1000_lock);

  // 1. 获取网卡期望的下一个发送描述符的索引
  uint32 idx = regs[E1000_TDT];

  // 2. 检查描述符的 E1000_TXD_STAT_DD 标志位，确认上一次发送是否已完成
  if ((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0) {
    // 硬件还没发完，说明发送环已满，返回错误让上层处理
    release(&e1000_lock);
    return -1;
  }

  // 3. 释放之前存在这个位置且已被发送出去的旧缓冲区
  if (tx_bufs[idx]) {
    kfree(tx_bufs[idx]);
    tx_bufs[idx] = 0;
  }

  // 4. 将新的缓冲区装载进描述符中
  tx_bufs[idx] = buf;
  tx_ring[idx].addr = (uint64)buf;
  tx_ring[idx].length = len;
  // 必须设置 EOP(End of Packet) 和 RS(Report Status) 标志
  tx_ring[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;

  // 5. 更新 TDT 寄存器，敲门通知网卡有数据要发送
  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  // 一次中断中可能会有多个包到来，因此使用 while 循环直到读完
  while (1) {
    // 1. 找到需要读取的下一个接收描述符的索引 (当前 RDT 的下一个)
    uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

    // 2. 检查 DD 标志位，如果未置位说明没有新的数据包到来
    if ((rx_ring[idx].status & E1000_RXD_STAT_DD) == 0) {
      break;
    }

    // 3. 将拿到的物理包从地址中解析出来
    char *buf = (char *)rx_ring[idx].addr;
    int len = rx_ring[idx].length;

    // 4. 将数据包上传给网络协议栈处理 (如 IP/UDP 层)
    net_rx(buf, len);

    // 5. 因为 buf 已经被交给了网络层，网卡的这个槽位空了，必须新 kalloc 一块内存补上
    buf = (char *)kalloc();
    if (!buf) {
      panic("e1000_recv: kalloc failed");
    }

    // 6. 重置这个描述符的地址，并将 status 清零，供硬件下一次接收使用
    rx_ring[idx].addr = (uint64)buf;
    rx_ring[idx].status = 0;

    // 7. 更新 RDT 寄存器，告诉网卡我们已经处理到了这里
    regs[E1000_RDT] = idx;
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}