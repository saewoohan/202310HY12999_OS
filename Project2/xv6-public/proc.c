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
int t_create = 0;
extern void forkret(void);
extern void trapret(void);

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
  if(t_create){
    p->pid = myproc()->pid;
    t_create= 0;
  }
  else{
    p->pid = nextpid++;
  }
  p->is_thread = 0;
  p->tid=0;
  p->maxtid = 0;
  p->mem_limit = 0;
  p->main_thread = p;
  p->next_thread = p;
  p->prev_thread = p;
  p->ustack = 0;
  for(int i = 0 ; i<NTHREAD; i++){
    p->stack[i] = -1;
  }
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    kfree(p->kstack);
    p->kstack = 0;
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
  p->main_thread = p;
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
  
  //memelimit 제한을 확인
  if(sz+n > curproc->mem_limit && curproc->mem_limit != 0){
    return -1;
  }
  
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  
  curproc->sz = sz;

  //모든 thread들의 sz를 동기화
  struct proc *p;
  
  for(p=ptable.proc; p<&ptable.proc[NPROC]; p++){
    if(p->main_thread == curproc->main_thread || p == curproc->main_thread){
      p->sz=curproc->sz;
    }
  }

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

  if((np->pgdir = copyuvm(curproc->main_thread->pgdir, curproc->main_thread->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->main_thread->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  //새로 만든 process의 page, memory limit를 설정
  np->mem_limit = curproc->mem_limit;
  np->pages = curproc->pages;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->main_thread->name, sizeof(curproc->main_thread->name));

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
  for(p = curproc->next_thread ; p != curproc; p = p->next_thread) {
    if(p->state != ZOMBIE) {
      for(fd = 0; fd < NOFILE; fd++) {
        if(p->ofile[fd]) {
          fileclose(p->ofile[fd]);
          p->ofile[fd] = 0;
        }
      }
      
      begin_op();
      iput(p->cwd);
      end_op();
      p->cwd = 0;
    }
  }

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

  kill_thread(curproc);
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
        p->maxtid = 0;
        p->is_thread = 0;
        p->main_thread = 0;
        p->mem_limit = 0;
        p->pages = 0;
        p->retval = 0;
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

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
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

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
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
  struct proc *p1;

  acquire(&ptable.lock);


  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;

      //kill all threads
      p = p->main_thread;
    
      for(p1 = p->next_thread; p1 != p; p1 = p1->next_thread){
        p1->killed = 1;

        if(p1->state == SLEEPING)
          p1->state = RUNNABLE;
      }
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

//set memory limit
int setmemorylimit(int pid, int limit) {
  struct proc *curproc = myproc();
  struct proc *p;
  struct proc *p1;
  acquire(&ptable.lock);

  //process가 존재하는지에 대한 여부
  int possible = 0;

  //인자의 pid와 동일한 process가 있는지 확인.
  for(p=ptable.proc; p < &ptable.proc[NPROC];p++){
	  if(p->pid==pid && p->is_thread == 0){
      possible = 1;
		  break;
    }
  }
  
  //process가 존재하지 않는 경우
  if(possible == 0){
    release(&ptable.lock);
    return -1;
  }

  //process는 존재하지만, limit의 값이 음수이거나 이미 process의 sz값이 limit보다 큰 경우
  if(limit < 0 || p->sz > limit){
    release(&ptable.lock);
    return -1;
  }

  //정상적으로 동작한 경우
  else{
    p = p->main_thread;
    p->mem_limit = limit;
    for(p1 = p->next_thread ; p1 != p; p1 = p1->next_thread)
      p1->mem_limit = limit;
    release(&ptable.lock);
  }
  return 0;
}

void print_all(void){
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc ; p< &ptable.proc[NPROC]; p++){
    //실행중인 프로세스가 아닌경우
    if(p->state == UNUSED || p->state == EMBRYO || p->state == ZOMBIE)
      continue;
    //쓰레드가 아닌 프로세스인 경우
    if(p->is_thread == 0 && p->killed != 1)
      cprintf("1.process name : %s     2.pid : %d     3.number of stack pages : %d      4.stack size : %d      5.memory limit : %d \n", p->name, p->pid, p->pages, p->sz, p->mem_limit);
  }
  release(&ptable.lock);
  return 0;
}

//thread_create
int thread_create(thread_t *thread, void *(*start_routine)(void*), void* arg)
{
  int i;
  struct proc *np;
  struct proc *curproc = myproc()->main_thread;
  uint sp, ustack[2];
  uint sz;

  //routine like fork
  //thread를 create하는 것을 체크
  sz = curproc->sz;
  if(sz+ 2*PGSIZE > curproc->mem_limit && curproc->mem_limit != 0){
    return -1;
  }
  t_create = 1;
  if ((np = allocproc()) == 0) {
    return -1;
  }

  
  acquire(&ptable.lock);
  np->mem_limit = curproc->mem_limit;
  
  for(int i=0;i<NOFILE;i++)
    if(curproc->ofile[i])
      np->ofile[i]=filedup(curproc->ofile[i]);
  np->cwd=idup(curproc->cwd);

  safestrcpy(np->name,curproc->name,sizeof(curproc->name));
  
  np->pgdir=curproc->pgdir;
  
  //routine like exec

  //stack에 빈공간이 있는지 확인
  for(i = 0; i < NTHREAD; i++) 
    if(curproc->stack[i] != -1)
	    break;

  //빈공간이 있다면, 그 자리에 할당
  if(i != NTHREAD) {
	 sp = curproc->stack[i];
	 np->ustack = sp;
	 curproc->stack[i] = -1;
  }
  //없다면, 새로 공간을 할당
  else {
    sz = PGROUNDUP(sz);

    //제한 사항이 없다면 allocuvm 수행
    if( (sz = allocuvm(curproc->pgdir, sz, sz + 2*PGSIZE)) == 0) {
      kfree(np->kstack);
      np->kstack=0;
      np->parent = 0;
      np->name[0] = 0;
      np->killed = 0;
      np->is_thread = 0;
      np->mem_limit = 0;
      np->pages = 0;
      np->retval = 0;
      np->pid = 0;
      np->tid = 0;
      np->state = UNUSED;
      for(i = 0; i < NTHREAD; i++)
        np->stack[i] = -1;
      release(&ptable.lock);
      return -1;
    }
    clearpteu(curproc->pgdir, (char*)(sz - 2*PGSIZE));

    sp = sz;
    curproc->sz = sz;
    np->ustack = sz;
  }
  
  //thread의 구조체 변수 초기화
  np->sz=curproc->sz;
  np->is_thread=1;
  np->pid = curproc->pid;
  np->parent = curproc->parent;
  np->main_thread = curproc;
  np->tid=curproc->maxtid++;
  *np->tf=*curproc->tf;
  np->next_thread = curproc;
  np->prev_thread = curproc->prev_thread;
  np->prev_thread->next_thread = np;
  curproc->prev_thread = np;

  *thread=np->tid;
  
  //ustack 할당 
  ustack[0] = 0xffffffff;
  ustack[1] = (uint) arg;

  sp -= 8;

  if(copyout(np->pgdir,sp,ustack,8)<0){
    kfree(np->kstack);
    np->kstack=0;
    np->parent = 0;
    np->name[0] = 0;
    np->killed = 0;
    np->is_thread = 0;
    np->mem_limit = 0;
    np->pages = 0;
    np->retval = 0;
    np->pid = 0;
    np->tid = 0;
    np->state = UNUSED;
    for(i = 0; i < NTHREAD; i++)
      np->stack[i] = -1;
    release(&ptable.lock);
    return -1;
  }
  
  // np->tf->eax=0;
   
  np->tf->eip=(uint)start_routine; //thread의 시작 위치
  np->tf->esp=sp;

  //process들의 모든 thread들의 stack size 동기화
  struct proc* p;

  for(p=ptable.proc;p<&ptable.proc[NPROC];p++){
    if(p->pid==np->pid){
      p->sz=np->sz;
    }
  }

  np->state=RUNNABLE;
  release(&ptable.lock);
  return 0;
}

//thread_exit
void thread_exit(void *retval){
  struct proc * curproc=myproc();
  struct proc *p;
  int fd;
  
  //Close all open files.
  for(fd=0;fd<NOFILE;fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd]=0;
    }
  }
  
  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd=0;
  acquire(&ptable.lock);
  
  wakeup1(curproc->main_thread);

  // Pass abandoned children to init.
  for(p=ptable.proc;p<&ptable.proc[NPROC];p++){
    if(p->parent==curproc){
      p->parent=initproc;
     if(p->state==ZOMBIE)
        wakeup1(initproc);
    }
  }
  
  // Jump into the scheduler, never to return.
  curproc->state=ZOMBIE;
  // thread의 return value를 설정
  curproc->retval=retval;
  sched();
  panic("zombie exit");
}

//thread_join
int thread_join(thread_t thread, void **retval){

  struct proc *p;
  struct proc *curproc = myproc()->main_thread;
  int i, havethread;

  acquire(&ptable.lock);

  for(;;){
    havethread = 0;
    //연결된 thread들을 순회
    for(p = curproc->next_thread ; p != curproc; p = p->next_thread){
      if(p->tid!=thread)
        continue;
      //인자로 들어온 tid와 같은 thread가 있는 경우
      havethread = 1;
      if(p->state == ZOMBIE){
        kfree(p->kstack);
        for(i = 0; i < NTHREAD; i++)
          if(curproc->stack[i] == -1)
            break;
        curproc->stack[i] = p->ustack;
        p->kstack = 0;
        p->next_thread->prev_thread = p->prev_thread;
        p->prev_thread->next_thread = p->next_thread;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->is_thread = 0;
        p->main_thread = 0;
        p->prev_thread = 0;
        p->next_thread = 0;
        p->mem_limit = 0;
        p->pages = 0;
        p->tid = 0;
        p->state = UNUSED;

        *retval=p->retval;
        release(&ptable.lock);
        return 0;
      }

    // No point waiting if we don't have any thread.
    if(!havethread || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for thread to exit. 
    }
    sleep(myproc(), &ptable.lock);  //DOC: wait-sleep

  }
    return -1;
}



void kill_thread(struct proc * curproc){

 // acquire(&ptable.lock);
  struct proc* p;
  int i;
  for(p = curproc->next_thread; p != curproc; p = p->next_thread){
      kfree(p->kstack);
      p->kstack=0;
      p->pid = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->is_thread = 0;
      p->mem_limit = 0;
      p->pages = 0;
      p->tid = 0;
      p->retval = 0;
      for(i = 0; i < NTHREAD; i++)
        p->stack[i] = -1;
      p->state = UNUSED;
  }
 //release(&ptable.lock);
}

//exit_thread
void exit_thread(struct proc * curproc){
  
  struct proc *p;
  struct proc *mthread = curproc->main_thread;
  int i, fd;  

  //close all open files
  for(p = curproc->next_thread ; p != curproc; p = p->next_thread){
      if(p->state != ZOMBIE) {
        for(fd = 0; fd < NOFILE; fd++) {
          if(p->ofile[fd]) {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
          }
        }
        
        begin_op();
        iput(p->cwd);
        end_op();
        p->cwd = 0;
      }
  }
  acquire(&ptable.lock);

  //exec을 호출한 thread가 main thread가 된다.
  curproc->pid = mthread->pid;
  curproc->parent = mthread->parent;
  curproc->killed = mthread->killed;  
  curproc->tid = 0;
  curproc->maxtid = 0;
  curproc->is_thread = 0;
  
  for(i = 0; i < NTHREAD; i++)
    curproc->stack[i] = -1;

  
  //자신을 제외한 모든 thread를 정리
  for(p = curproc->next_thread; p != curproc; p = p->next_thread){
      kfree(p->kstack);
      p->kstack=0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->is_thread = 0;
      p->mem_limit = 0;
      p->pages = 0;
      p->retval = 0;
      p->pid = 0;
      p->tid = 0;
      p->state = UNUSED;
      for(i = 0; i < NTHREAD; i++)
        p->stack[i] = -1;
  }
  curproc->main_thread = curproc;
  curproc->next_thread = curproc;
  curproc->prev_thread = curproc;
  release(&ptable.lock);

}