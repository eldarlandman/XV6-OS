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
extern void endInjectedSigRet(void);
extern void injectedSigRet(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  struct proc * p;
  int i;
  initlock(&ptable.lock, "ptable");
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    p->pendingSignals.head = &(p->pendingSignals.frames[0]);
    for(i = 0; i < 9; i++)
      p->pendingSignals.frames[i].next = &p->pendingSignals.frames[i + 1];
    p->pendingSignals.frames[9].next = 0;
    p->handlingSignal = 0;
  }
  release(&ptable.lock);
}

int 
allocpid(void) 
{
  int pid;
  do {
    pid=nextpid;
  }while(!cas(&nextpid ,pid, pid+1));
    
  return pid;
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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(cas(&(p->state), UNUSED, EMBRYO))
      goto found;
  return 0;

found:
  
  p->pid = allocpid();

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

  p->state = RUNNABLE;
  
  p->handler = (void*) -1;
  p->pendingSignals.head->used = 0;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
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

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));
 
  pid = np->pid;
  np->handler = proc->handler;
  np->handlingSignal = 0;
  np->pendingSignals.head = &(np->pendingSignals.frames[0]);
  for (i = 0; i < 10; i++)
    np->pendingSignals.frames[i].used = 0;
    

  // lock to force the compiler to emit the np->state write last.
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
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  proc->state = ZOMBIE;

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  
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

  acquire(&ptable.lock);
  for(;;){
    proc->chan = (int)proc;
    proc->state = SLEEPING;    
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;

        proc->chan = 0;
        proc->state = RUNNING;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      proc->chan = 0;
      proc->state = RUNNING;      
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sched();
  }
}

void 
freeproc(struct proc *p)
{
  if (!p || p->state != ZOMBIE)
    panic("freeproc not zombie");
  kfree(p->kstack);
  p->kstack = 0;
  freevm(p->pgdir);
  p->killed = 0;
  p->chan = 0;
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
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
      if (p->state == ZOMBIE)
        freeproc(p);
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
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
    initlog();
  }
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
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
  proc->chan = (int)chan;
  proc->state = SLEEPING;


  sched();

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
    if(p->state == SLEEPING && p->chan == (int)chan){
      // Tidy up.
      p->chan = 0;
      p->state = RUNNABLE;
    }
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


sig_handler sigset(sig_handler sig)
{
  proc->handler = sig;
  return sig;
}

int push(struct cstack * cstack, int sender_pid, int recepient_pid, int value)
{
  struct cstackframe * oldHead;
  struct cstackframe * newHead;
  
  do
  {
    oldHead = cstack->head;
    if (oldHead == &cstack->frames[9])
      return 0;
    else if (oldHead->used)
      newHead = oldHead + 1;
    else
      newHead = oldHead;
  }while (!cas((int *)(&cstack->head), (int)oldHead, (int)newHead));
  
  do
  {
    newHead->recepient_pid = recepient_pid;
  } while (!cas(&newHead->used, 0, 0));
  newHead->value = value;
  newHead->sender_pid = sender_pid;
  newHead->used = 1;
  return 1;
  
  /*struct cstackframe * oldHead;
  struct cstackframe * newHead;
  int prevVal, prevSender;
  do{
	oldHead = cstack->head;
	if (oldHead->used == 10)
	  return 0;
	else
	{
	  if (!oldHead->used)
	  {
	      newHead = oldHead;
	  }
	  else
	  {
	      newHead = oldHead->next;
	  }
	  newHead->used = oldHead->used + 1;
	  newHead->recepient_pid = recepient_pid;
	  prevVal = newHead->value;
	  prevSender = newHead->sender_pid;
	}
  }while (! (cas((int *)(&(cstack->head)) , (int)oldHead , (int)newHead) &&
		  cas(&(newHead->sender_pid) , prevSender , sender_pid) &&
		  cas(&(newHead->value) , prevVal , value)));
  */
  //the next three lines update sender_pid, recepient_pid, value  process's fields  
  
}

struct cstackframe * pop(struct cstack * cstack)
{
  struct cstackframe * oldHead = cstack->head;
  if (!oldHead->used && oldHead == cstack->frames)
    return 0;
  
  struct cstackframe * newHead;
  do
  {
    oldHead = cstack->head;
    if (oldHead == cstack->frames)
      newHead = oldHead;
    else
      newHead = oldHead - 1;
  }while (!(cas(&oldHead->used, 1, 1) && cas((int *)(&cstack->head), (int)oldHead, (int)newHead)));
  return oldHead;
  
/*  struct cstackframe * old_csf;
  struct cstackframe * newHead;
  int old_used;
  int newHeadLoc=0;
    do{
	  old_csf=cstack->head;
	  old_used=old_csf->used;
	  
	  if (old_csf->used == 0)
	    return 0;	  
	  else{	    
	      newHeadLoc = old_csf->used - 2;
	  }
	  
	  if (newHeadLoc < 0)
	  {
	      newHead = &cstack->frames[0];
	  }
	  else
	  {
	      newHead = &cstack->frames[newHeadLoc];
	  }
    
    }while( ! ( && cas( (int*) &cstack->head ,(int)old_csf, (int)newHead)   && cas( (int*) &old_csf->used , (int)old_used, 0)) );
      
    return newHead;*/
  }


int sigsend(int dest_pid, int value)
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {   
    if (p->pid == dest_pid)
      break;
  }
  return push(&(p->pendingSignals), proc->pid, dest_pid, value);
}

void sigret(void)
{
  memmove(proc->tf, &proc->old_tf, sizeof(proc->old_tf) );
  proc->handlingSignal = 0;
}

int sigpause(void)
{
  //TODO
  return -1;
}

void applySig(void){

      struct cstackframe * csf;
      if (proc && proc->pendingSignals.head->used>0 && (proc->tf->cs & 3) == 3)
      { //check if theres any pending signals which must be handled   
	if ((int)proc->handler == -1)
	{
	  do
	  {
	    csf = pop(&proc->pendingSignals);
	  }while (csf);
	}
	else if (!proc->handlingSignal)
	{
	  proc->handlingSignal = 1;
	  memmove(&proc->old_tf, proc->tf, sizeof(proc->old_tf)); //deep backup of  registers before sig_handler executed
	  struct proc *p = proc; //for debugging
	  csf = pop(&proc->pendingSignals);
	  int sigVal = csf->value;
	  int sigSender = csf->sender_pid;
	  csf->used = 0;
	  //forcing user to execute SIG_RET
	  uint sigRetSysCallCodeSize = endInjectedSigRet - injectedSigRet; //declared as global in trapasm.S
	  void * espBackup = (void *)(proc->tf->esp - sigRetSysCallCodeSize); //backup the return address from sig_handler which we wish to postpone after calling to sig_ret
	  memmove((void *)(proc->tf->esp - sigRetSysCallCodeSize),  injectedSigRet, sigRetSysCallCodeSize);//copy the injected code to the users stack
	  proc->tf->esp -= sigRetSysCallCodeSize;
	  memmove((int *)(proc->tf->esp - sizeof(sigVal)),  &(sigVal), sizeof(sigVal)); //push sig_handler args to the stack
	  proc->tf->esp -= sizeof(sigVal);
	  memmove( (int *)((proc->tf->esp) - sizeof(sigSender)),  &(sigSender), sizeof(sigSender));
	  proc->tf->esp -= sizeof(sigSender);
	  memmove( (void **)((proc->tf->esp) - sizeof(void *)), &espBackup, sizeof(espBackup) ); //push "ret address" to be the begining of sig_ret code
	  proc->tf->esp -= sizeof(void*);
	  void * ebpBackup = (void *)(proc->tf->ebp);
	  memmove( (void **)((proc->tf->esp) - sizeof(void *)), &ebpBackup, sizeof(ebpBackup) ); //push "ret address" to be the begining of sig_ret code
	  proc->tf->ebp = proc->tf->esp;
	  proc->tf->eip = (uint)proc->handler;    //jump to sig handler code
	  p++;
	}
      }
  }  