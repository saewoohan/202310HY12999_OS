#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
extern uint ticks; //priority boosting을 위한 global ticks
extern uint ctime; //계속 증가하고 있는 global ticks
int queueLevel = 0; //현재 진행하고 있는 queueLevel
const int Q0_ticks = 4; //Q0에서의 time quantum  
const int Q1_ticks = 6; //Q1에서의 time quantum
const int Q2_ticks = 8; //Q2에서의 time quantum
const int global_Limit = 100; //priority_boosting을 위한 global time quantum

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  SETGATE(idt[128], 1 , SEG_KCODE<<3, vectors[128], DPL_USER);
  SETGATE(idt[129], 1 , SEG_KCODE<<3, vectors[129], DPL_USER);
  SETGATE(idt[130], 1 , SEG_KCODE<<3, vectors[130], DPL_USER);
  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }
  
  if(tf->trapno == 128){
    exit();
    return;
  }
  //129번 인터럽트일 경우 schedulerLock을 무조건 실행
  if(tf->trapno == 129){
    schedulerLock(2019067429);
    return;
  }
  //130번 인터럽트일 경우 schedulerUnlock을 무조건 실행
  if(tf->trapno == 130){
    schedulerUnlock(2019067429);
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      //타임 인터럽트가 발생할 때 마다 글로벌 틱과 프로세스의 틱, ctime을 증가시켜줌
      ticks++;
      ctime++;
      if(myproc()){
        myproc()->ticks++;
      }
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();


    //글로벌 틱이 100틱을 넘어 갈 때, priority_boosting을 실행
    if(myproc() && myproc()->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER && ticks > global_Limit){
      ticks = 0;
      priority_boosting();
     }
    //타임 인터럽트가 발생한 경우
    if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER){
      //L0큐에 존재하는 프로세스가 time quantum을 모두 사용했을 때
      if(myproc()->queue == 0 && myproc()->ticks > Q0_ticks && queueLevel == 0){
        //프로세스의 time quantum을 초기화 시켜주고 yield0_1 호출
        myproc()->ticks = 0;
        yield0_1();
      }
      //L1큐에 존재하는 프로세스가 time quantum을 모두 사용했을 때
      else if(myproc()->queue == 1 && myproc()->ticks > Q1_ticks && queueLevel == 1){
        //프로세스의 time quantum을 초기화 시켜주고 yield0_1 호출
        myproc()->ticks = 0;
        yield0_1();
      }
      //L2큐에 존재하는 프로세스가 time quantum을 모두 사용했을 때
      else if(myproc()->queue == 2 && myproc()->ticks > Q2_ticks && queueLevel == 2){
        //프로세스의 time quantum을 초기화 시켜주고 yield2 호출
        myproc()->ticks = 0;
        yield2();
      }
      //위의 모든 상황이 아니라면 yield 호출 (1ticks마다 CPU 양도)
      else{
        yield();
      }
     }

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

}

