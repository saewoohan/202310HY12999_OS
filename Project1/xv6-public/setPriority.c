#include "types.h"
#include "defs.h"
#include "proc.h"
int
setPriority(int pid, int priority)
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc ; p < &ptable.proc[NPROC]; p++){
      if(p->pid == pid){
        p->priority = priority;
        break;
      }
  }
  release(&ptable.lock);
  return pid;
}