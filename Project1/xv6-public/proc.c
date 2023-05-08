#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
extern void priority_boosting(void);
extern int queueLevel; //현재 스케줄링 중인 큐의 레벨을 나타냄
uint ticks; //priority_boosting을 위한 global ticks
uint ctime; //프로세스가 각 큐에 들어가는 시간을 저장하기 위한 ticks
int L0 = 0; //L0큐에 존재하는 프로세스의 개수
int L1 = 0; //L1큐에 존재하는 프로세스의 개수
int L2 = 0; //L2큐에 존재하는 프로세스의 개수
int Lock = 0; //schedulerLock 후 다시 MLFQ로 돌아온 프로세스를 체크
int isLock; //scheduelrLock이 발생했는지 확인하는 변수
static void wakeup1(void *chan);


void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;
  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;


  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  //변수 초기화
  p->queue = 0;
  p->priority = 3;
  p->ticks = 0;
  p->ctime = ctime;
  release(&ptable.lock);
  //새로 프로세스가 할당 받을 때, L0큐의 프로세스 개수는 증가
  L0++;
  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    struct proc *p1;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    //레벨별로 프로세스가 몇 개 존재하는지 저장하기 위한 변수  
    int Lock_p = 0;
    int L_p0 = 0;
    int L_p1 = 0;
    int L_p2 = 0;   
    //각 레벨별로 프로세스가 몇 개 존재하는지 세어줌
    for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
      if((p1->state == RUNNABLE || p1->state == RUNNING) && p1->pid > 0){
          //schedulerLock 상태인 프로세스가 있을 경우
          if(p1->queue == -1){
            //해당 프로세스가 종료 혹은 priority_boosting이 될 때 까지 실행
              for(;;){
                //프로세스가 종료된 경우 priority_boosting 종료
                if(p1->pid < 1 || p1->queue != -1 || p1->state != RUNNABLE){
                  isLock = 0;
                  goto end;
                }
                c->proc = p1;
                switchuvm(p1);
                p1->state = RUNNING;

                swtch(&(c->scheduler), p1->context);
                switchkvm();
                c->proc = 0;
              }
              goto end;
          }
        //schedulerLock 상태에서 MLFQ로 돌아온 프로세스를 체크해줌
        if(p1->isLock == 1){
          Lock_p++;
        }
        //L0큐에 존재하는 프로세스의 개수를 세어줌
        if(p1->queue == 0){
          L_p0++;
        }
        //L1큐에 존재하는 프로세스의 개수를 세어줌
        else if(p1->queue == 1){
          L_p1++;
        }
        //L2큐에 존재하는 프로세스의 개수를 세어줌
        else if(p1->queue == 2){
           L_p2++;
        }
      }
    }
    
    //전역 변수 Lock, L0, L1, L2에 큐 마다의 프로세스 개수를 저장해줌
    Lock = Lock_p;
    L0 = L_p0;
    L1 = L_p1;
    L2 = L_p2;

    //L0큐에 프로세스가 존재하는 경우, Round Robin 방식
    if(L0 > 0){
      //schedulerLock 상태에서 다시 MLFQ로 돌아온 프로세스가 있는 경우
      if(Lock > 0){
        for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
          //프로세스가 RUNNABLE 상태가 아니면 continue
          if(p->state != RUNNABLE)
            continue;
          //프로세스의 isLock 변수가 1이 아니라면
          if(p->isLock != 1){
            continue;
          }
          //스케줄링 도중 schedulerLock이 발생하면 해당 큐의 스케줄링 종료
          if(isLock == 1){
            goto end;
          }
          //context switching
          c->proc = p;
          switchuvm(p);
          p->state = RUNNING;

          swtch(&(c->scheduler), p->context);
          switchkvm();

          c->proc = 0;
        }
      }
      //스케줄링 중인 큐 레벨을 0으로 설정
      queueLevel = 0;
      
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        //프로세스가 RUNNABLE 상태가 아니면 continue
        if(p->state != RUNNABLE){
          continue;
        }
        //프로세스가 L0큐에 속하지 않으면 continue
        if(p->queue != 0){
          continue;
        }
        //스케줄링 도중 schedulerLock이 발생하면 해당 큐의 스케줄링 종료
        if(isLock == 1){
          goto end;
        }

        //다시 스케줄러로 돌아온 프로세스는 앞서서 CPU를 사용했기에 continue;
        if(Lock > 0 && p->isLock == 1){
          continue;
        }
        //context switching
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        c->proc = 0;

      }
    }

    //L0큐에 프로세스가 존재하지 않고 L1큐에 프로세스가 존재하는 경우, Round Robin 방식
    else if(L1 > 0){
      //schedulerLock 상태에서 다시 MLFQ로 돌아온 프로세스가 있는 경우
      if(Lock > 0){
        for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
          //프로세스가 RUNNABLE 상태가 아니면 continue
          if(p->state != RUNNABLE)
            continue;
          //프로세스의 isLock 변수가 1이 아니라면
          if(p->isLock != 1){
            continue;
          }
          //스케줄링 도중 schedulerLock이 발생하면 해당 큐의 스케줄링 종료
          if(isLock == 1){
            goto end;
          }
          //context switching
          c->proc = p;
          switchuvm(p);
          p->state = RUNNING;

          swtch(&(c->scheduler), p->context);
          switchkvm();

          c->proc = 0;
        }
      }
      //스케줄링 중인 큐 레벨을 1로 설정
      queueLevel = 1;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        //프로세스가 RUNNABLE 상태가 아니라면 continue
        if(p->state != RUNNABLE){
          continue;
        }
        //만약 순회 중 Level이 0인 프로세스가 있다면, L1큐의 스케줄링을 종료
        if(L0 > 0)
          goto end;
        //프로세스의 큐 레벨이 1이 아니라면 continue
        if(p->queue != 1)
          continue;
        //스케줄링 도중 schedulerLock이 발생하면 해당 큐의 스케줄링 종료
        if(isLock == 1){
          goto end;
        }
        //다시 스케줄러로 돌아온 프로세스는 앞서서 CPU를 사용했기에 continue;
        if(Lock > 0 && p->isLock == 1){
          continue;
        }

        //context switching
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        c->proc = 0;

      }
    }

    //L0,L1큐에 프로세스가 없고, L2에 프로세스가 있다면 priority를 기반으로 한 스케줄링 실행
    else if(L2 > 0){
      //비교를 위한 변수 설정
      int priority = 100;
      uint p_time = 4294967295;
      p = 0;
      //스케줄링 중인 큐 레벨을 2로 설정
      queueLevel = 2;
      struct proc *p1;
      for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++) {
        //만약 순회 중 Level이 0, 1인 프로세스가 있다면, 종료
        if(L1 > 0 || L0 > 0)
          goto end;
        //우선순위가 더 높은 프로세스가 있다면, 비교 후 먼저 실행
        if(p1->state == RUNNABLE && p1->priority < priority && p1->queue == 2) {
          priority = p1->priority;
          p_time = p1->ctime;
          p = p1;
        }
        //우선 순위가 같은 프로세스 끼리는 FCFS 방식이기에 비교 후 먼저 실행
        else if(p1->state == RUNNABLE && p1->priority == priority && p1->queue == 2) {
          if(p1->ctime < p_time) {
            p_time = p1->ctime;
            priority = p1->priority;
            p = p1;
          }
        }
        //스케줄링 도중 schedulerLock이 발생하면 해당 큐의 스케줄링 종료
        if(isLock == 1){
          goto end;
        }
      }

      if(p != 0) {
        //context switching
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        switchkvm();
        c->proc = 0;
      }
    }
    end:
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

//yield 시스템 콜
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

//L2큐에 있는 프로세스에서 yield 실행 된 경우 priority를 감소시켜줌
void
yield2(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  if(myproc()->priority > 0){
    myproc()->priority--;
  }
  sched();
  release(&ptable.lock);
}


//L0큐 혹은 L1큐에 있는 프로세스에서 yield가 실행 된 경우 큐 레벨을 늘려줌.
void
yield0_1(void)
{
  acquire(&ptable.lock);
  myproc()->state = RUNNABLE;
  //L1, L2큐에 존재하는 프로세스의 개수를 조정해줌
  if(myproc()->queue == 1){
    L1--;
    L2++;
  }
  //L0, L1큐에 존재하는 프로세스의 개수를 조정해줌
  if(myproc()->queue == 0){
    L0--;
    L1++;
  }
  // myproc()->isLock = 0;
  //해당 큐에 할당된 시간을 나타내는 ctime 변수 조정
  myproc()->ctime = ctime;
  //큐 레벨 증가
  myproc()->queue++;
  sched();
  release(&ptable.lock);
}

//priority_boosting
void
priority_boosting(void)
{
  struct proc *p;
  acquire(&ptable.lock);
  //ptable을 모두 돌면서 time quantum, priority, 큐 레벨을 초기화 시켜줌
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    //schedulerLock이 되었던 process가 있었을 경우 제일 먼저 스케줄링 해주기 위해 체크 해줌.
    if(p->queue == -1){
      p->isLock = 1;
    }
    p->ticks = 0;
    p->priority = 3;
    p->queue = 0;
    isLock = 0;
    p->ctime = ctime;
  }
  queueLevel = 0;
  release(&ptable.lock);
}


// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
cps()
{
  struct proc *p;
  //Loop over process table looking for process with pid.
  acquire(&ptable.lock);
  cprintf("name \t pid \t state \t priority \t queue \t ticks \t globalTicks\n");
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING){
    //  cprintf("%s \t %d \t SLEEPING \t %d \t %d \t %d \t %d \t %d \t %d \t %d\n ", p->name, p->pid, p->priority, p->queue, p->ticks, ticks, p->l0, p->l1, p->l2);
    }
    else if(p->state == RUNNING){
    //  cprintf("%s \t %d \t RUNNING \t %d \t %d \t %d \t %d \t %d \t %d \t %d\n ", p->name, p->pid, p->priority, p->queue, p->ticks, ticks, p->l0, p->l1, p->l2);
    }
    else{ 
    //  cprintf("%s \t %d \t RUNNABLE \t %d \t %d \t %d \t %d \t %d \t %d \t %d\n", p->name, p->pid, p->priority, p->queue, p->ticks, ticks,  p->l0, p->l1, p->l2);
    }
  }
  release(&ptable.lock);
  return 23;
}

//setPriority
void
setPriority(int pid, int priority)
{
  struct proc *p;
  int possible = 0;
  int p_possible = 1;
  //priority가 0~3의 범위가 아님을 체크
  if(priority < 0 || priority > 3){
    p_possible = 0;
  }
  //priority가 0~3의 범위가 아니라면 오류 메시지 출력 후 아무런 변화도 안줌
  if(p_possible == 0){
    cprintf("Since the PRIORITY is invalid, the program will continue to execute.\n");
  }
  //priority가 0~3의 범위일 때
  else{
    acquire(&ptable.lock);
    //ptable을 돌면서 pid가 인자와 같은 프로세스를 찾고 priority를 바꾸어 줌
    for(p = ptable.proc ; p < &ptable.proc[NPROC]; p++){
        if(p->pid == pid){
          p->priority = priority;
          possible = 1;
          break;
       }
   }
   //인자로 들어온 pid와 같은 프로세스가 없으면 오류메시지 출력 후 아무런 변화도 안줌
   if(possible == 0){
     cprintf("Since the PID is invalid, the program will continue to execute.\n");
   }
   release(&ptable.lock);
  }
}

//현재 프로세스의 level을 return
int 
getLevel(void)
{
  struct proc *p = myproc();
  return p->queue;
}

//현재 프로세스의 pid를 return
int 
getPid(void)
{
  struct proc *p = myproc();
  return p->pid;
}


//schdulerLock
void
schedulerLock(int password)
{
  
  struct proc *p = myproc();
  //인자로 받은 password와 학번이 같은 경우
  if(password == 2019067429){
    if(isLock != 0){
       cprintf("There is already a schedulerLock process\n");
    }
    else{
      acquire(&tickslock);
      //global ticks를 초기화
      ticks = 0;
      wakeup(&ticks);
      release(&tickslock);
      acquire(&ptable.lock);
      //해당 프로세스의 큐 레벨을 -1로 체크해줌
      p->queue = -1;
      //schedulerLock이 발생 했음을 체크해줌
      isLock = 1;
      release(&ptable.lock);
    }
  }
  //다를 경우 오류 메시지를 출력하고 현재 process의 pid, time_quantum, queue를 출력하고 강제 종료
  else{
    //p가 schedulerLock 상태인 프로세스인 경우 예외 처리를 위해 작성
    if(p->queue == -1){
      p->queue =0;
      isLock = 0;
    }
    cprintf("Invalid password, exiting the process.\n");
    cprintf("pid \t time_quantum \t level \n");
    cprintf("%d \t %d \t \t %d \n", myproc()->pid, myproc()->ticks, myproc()->queue);
    exit();
  }
}

//schdulerUnlock
void
schedulerUnlock(int password)
{
  struct proc *p = myproc();
  //인자로 받은 password와 학번이 같은 경우
  if(password == 2019067429){
    //프로세스의 큐 레벨이 -1, 즉 schedulerLock 상태가 아닌 경우 오류 메시지 출력 후 exit 호출
    if(myproc()->queue != -1){
      cprintf("This Process is not Locked, exiting the process.\n");
      cprintf("pid \t time_quantum \t level \n");
      cprintf("%d \t %d \t \t %d \n", myproc()->pid, myproc()->ticks, myproc()->queue);
      exit();
    }
    acquire(&ptable.lock);
    //프로세스의 queue, ticks, priority를 초기화
    p->queue = 0;
    p->ticks = 0;
    p->priority = 3;
    //scheduerLock 상태에서 MLFQ로 돌아왔음을 체크
    p->isLock = 1;
    //schedulerLock 상태가 아님을 체크
    isLock = 0;
    release(&ptable.lock);
  }
  else{
    //p가 schedulerLock 상태인 프로세스인 경우 예외 처리를 위해 작성
    if(p->queue == -1){
      p->queue =0;
      isLock = 0;
    }
    //다를 경우 오류 메시지를 출력하고 현재 process의 pid, time_quantum, queue를 출력하고 강제 종료
    cprintf("Invalid password, exiting the process.\n");
    cprintf("pid \t time_quantum \t level \n");
    cprintf("%d \t %d \t \t %d \n", myproc()->pid, myproc()->ticks, myproc()->queue);
    exit();
  }
  
}
