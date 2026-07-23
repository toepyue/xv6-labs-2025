#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

// ============================================
// 新增：UDP 套接字端口管理器与队列缓冲区
// ============================================
#define MAX_SOCKETS 16
#define MAX_PACKETS 16

struct socket {
  int inuse;
  uint16 port;                   // 绑定的 UDP 端口号
  char *packets[MAX_PACKETS];    // 收到的数据包队列
  int head;                      // 读队列头
  int tail;                      // 写队列尾
};

struct socket sockets[MAX_SOCKETS];

void
netinit(void)
{
  initlock(&netlock, "netlock");
}

//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
//
// bind(int port)
//
uint64
sys_bind(void)
{
  int port;
  argint(0, &port); 

  acquire(&netlock);
  // 遍历找到一个空闲的套接字槽位
  for (int i = 0; i < MAX_SOCKETS; i++) {
    if (!sockets[i].inuse) {
      sockets[i].inuse = 1;
      sockets[i].port = port;
      sockets[i].head = 0;
      sockets[i].tail = 0;
      release(&netlock);
      return 0;
    }
  }
  release(&netlock);
  return -1; // 没有空闲的套接字
}

//
// unbind(int port)
//
uint64
sys_unbind(void)
{
  int port;
  argint(0, &port); // 直接获取参数，去掉了 if 检查

  acquire(&netlock);
  for (int i = 0; i < MAX_SOCKETS; i++) {
    if (sockets[i].inuse && sockets[i].port == port) {
      sockets[i].inuse = 0;
      // 必须将还没来得及读走的数据包释放掉，防止内存泄漏
      while (sockets[i].head != sockets[i].tail) {
        kfree(sockets[i].packets[sockets[i].head]);
        sockets[i].head = (sockets[i].head + 1) % MAX_PACKETS;
      }
      release(&netlock);
      return 0;
    }
  }
  release(&netlock);
  return -1;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
//
uint64
sys_recv(void)
{
  int dport;
  uint64 src_addr;
  uint64 sport_addr;
  uint64 buf_addr;
  int maxlen;

  argint(0, &dport);
  argaddr(1, &src_addr);
  argaddr(2, &sport_addr);
  argaddr(3, &buf_addr);
  argint(4, &maxlen);

  struct socket *sock = 0;

  acquire(&netlock);
  // 根据传入的 dport 找到匹配的绑定套接字
  for (int i = 0; i < MAX_SOCKETS; i++) {
    if (sockets[i].inuse && sockets[i].port == dport) {
      sock = &sockets[i];
      break;
    }
  }

  if (!sock) {
    release(&netlock);
    return -1; // 端口未绑定
  }

  // 如果队列为空，则进入 sleep 阻塞等待数据
  while (sock->head == sock->tail) {
    if (myproc()->killed) {
      release(&netlock);
      return -1;
    }
    sleep(sock, &netlock);
  }

  // 醒来并发现有数据，从队列头部提取数据包
  char *packet = sock->packets[sock->head];
  sock->head = (sock->head + 1) % MAX_PACKETS;
  
  // 核心：在向用户空间 copyout 数据前，必须释放自旋锁，防止缺页死锁
  release(&netlock);

  // 解析并剥离各类协议头
  struct eth *eth = (struct eth *)packet;
  struct ip *ip = (struct ip *)(eth + 1);
  struct udp *udp = (struct udp *)(ip + 1);
  char *payload = (char *)(udp + 1);

  // 处理大端序到本地端序的转换
  uint32 src = ntohl(ip->ip_src);
  uint16 sport = ntohs(udp->sport);
  int copylen = ntohs(udp->ulen) - sizeof(struct udp);
  
  if (copylen > maxlen) {
    copylen = maxlen;
  }

  // 往用户态空间写回解析出的信息和数据负载 (copyout 有返回值，保留 if 检查)
  struct proc *p = myproc();
  if (copyout(p->pagetable, src_addr, (char *)&src, sizeof(src)) < 0 ||
      copyout(p->pagetable, sport_addr, (char *)&sport, sizeof(sport)) < 0 ||
      copyout(p->pagetable, buf_addr, payload, copylen) < 0) {
    kfree(packet);
    return -1;
  }

  // 将成功被读取走的数据包的物理内存释放掉
  kfree(packet);
  return copylen;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  struct eth *eth = (struct eth *)buf;
  struct ip *ip = (struct ip *)(eth + 1);

  // 1. 甄别是否是我们要拦截的 UDP 协议 (抛弃 TCP 等不关心的协议)
  if (ip->ip_p != IPPROTO_UDP) {
    kfree(buf);
    return;
  }

  struct udp *udp = (struct udp *)(ip + 1);
  // 必须使用 ntohs 对目标的端口（大端序）进行倒序解构
  uint16 dport = ntohs(udp->dport);

  acquire(&netlock);
  // 2. 在队列中搜寻是否有用户态程序正绑定监听了这个端口
  for (int i = 0; i < MAX_SOCKETS; i++) {
    if (sockets[i].inuse && sockets[i].port == dport) {
      int next_tail = (sockets[i].tail + 1) % MAX_PACKETS;
      
      // 3. 检查当前端口的消息队列是否已经被 16 个包占满了
      if (next_tail == sockets[i].head) {
        // 满载时毫不犹豫地丢弃，防止内存打满
        kfree(buf);
      } else {
        // 未满，推入队列
        sockets[i].packets[sockets[i].tail] = buf;
        sockets[i].tail = next_tail;
        
        // 4. 唤醒有可能正在被 recv 阻塞等待的进程
        wakeup(&sockets[i]);
      }
      release(&netlock);
      return;
    }
  }
  release(&netlock);
  
  // 发现压根没有进程监听这个端口，默默丢弃
  kfree(buf);
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}