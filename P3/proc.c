#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#ifdef CS333_P2
#include "uproc.h"
#endif //CS333_P2


static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};

#ifdef CS333_P3
struct ptrs {
  struct proc *head;
  struct proc *tail;
};
#endif // CS333_P3

#ifdef CS333_P3
#define statecount NELEM(states)
#endif //CS333_P3

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
#ifdef CS333_P3
  struct ptrs list[statecount];
#endif
} ptable;

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);
#ifdef CS333_P3
static void initProcessLists(void);
static void initFreeList(void);
static void stateListAdd(struct ptrs*, struct proc*);
static int stateListRemove(struct ptrs*, struct proc* p);
static void assertState(struct proc* p, enum procstate state);
#endif // CS333_P3

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
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
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
#ifdef CS333_P3
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;
  int rc;

  acquire(&ptable.lock);
  p = ptable.list[UNUSED].head;
  if(!p){
    release(&ptable.lock);
    return 0;
  }
  rc = stateListRemove(&ptable.list[UNUSED], p);
  if(rc == -1)
    panic("Error: not in unused list");
  assertState(p, UNUSED);
  p->state = EMBRYO;
  p->pid = nextpid++;
  stateListAdd(&ptable.list[EMBRYO], p);
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    acquire(&ptable.lock);
    rc = stateListRemove(&ptable.list[EMBRYO], p);
    if(rc == -1)
      panic("Error: not in embryo list");
    assertState(p, EMBRYO);
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
    release(&ptable.lock);
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
  p->start_ticks = ticks;
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
  return p;
}
#else
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  int found = 0;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  }
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);

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
#ifdef CS333_P1
  p->start_ticks = ticks;
#endif // CS333_P1
#ifdef CS333_P2
  p->cpu_ticks_total = 0;
  p->cpu_ticks_in = 0;
#endif // CS333_P2
  return p;
}
#endif
//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

#ifdef CS333_P3
  acquire(&ptable.lock);
  initProcessLists();
  initFreeList();
  release(&ptable.lock);
#endif // CS333_P3
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
#ifdef CS333_P3
  int rc = stateListRemove(&ptable.list[EMBRYO], p);
  if(rc == -1)
    panic("Error: not in embryo list");
  assertState(p, EMBRYO);
#endif //CS333_P3
  p->state = RUNNABLE;
#ifdef CS333_P2
  p->uid = DEFAULTID;
  p->gid = DEFAULTID;
#endif // CS333_P2
#ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], p);
#endif // CS333_P3
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
  int i;
  uint pid;
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
#ifdef CS333_P3
    acquire(&ptable.lock);
    int rc = stateListRemove(&ptable.list[EMBRYO], np);
    if(rc == -1)
      panic("Error: not in embryo list");
    assertState(np, EMBRYO);
#endif // CS333_P3
    np->state = UNUSED;
#ifdef CS333_P3
    stateListAdd(&ptable.list[UNUSED], np);
    release(&ptable.lock);
#endif
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
#ifdef CS333_P2
  np->uid = curproc->uid;
  np->gid = curproc->gid;
#endif // CS333_P2

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
#ifdef CS333_P3
  int rc = stateListRemove(&ptable.list[EMBRYO], np);
  if(rc == -1)
    panic("Error: not in embryo list");
  assertState(np, EMBRYO);
#endif // CS333_P3
  np->state = RUNNABLE;
#ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], np);
#endif
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
#ifdef CS333_P3
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files
  for (fd = 0; fd < NOFILE; fd++) {
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
  p = ptable.list[EMBRYO].head;
  while(p){
    assertState(p, EMBRYO);
    if(p->parent == curproc){
      p->parent = initproc;
    }
    p = p->next;
  }
  p = ptable.list[RUNNING].head;
  while(p){
    assertState(p, RUNNING);
    if(p->parent == curproc){
      p->parent = initproc;
    }
    p = p->next;
  }
  p = ptable.list[RUNNABLE].head;
  while(p){
    assertState(p, RUNNABLE);
    if(p->parent == curproc){
      p->parent = initproc;
    }
    p = p->next;
  }
  p = ptable.list[SLEEPING].head;
  while(p){
    assertState(p, SLEEPING);
    if(p->parent == curproc){
      p->parent = initproc;
    }
    p = p->next;
  }
  p = ptable.list[ZOMBIE].head;
  while(p){
    assertState(p, ZOMBIE);
    if(p->parent == curproc){
      p->parent = initproc;
      wakeup1(initproc);
    }
    p = p->next;
  }
  // Jump into the scheduler, never to return.
  int rc = stateListRemove(&ptable.list[RUNNING], curproc);
  if(rc == -1)
    panic("Error: not in running list");
  assertState(curproc, RUNNING);
  curproc->state = ZOMBIE;
  stateListAdd(&ptable.list[ZOMBIE], curproc);
  sched();
  panic("zombie exit");
}
#else
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++) {
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
#endif // CS333_P3

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P3
int wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through lists looking for zombie children
    havekids = 0;
    p = ptable.list[EMBRYO].head;
    while(p){
      assertState(p, EMBRYO);
      if(p->parent == curproc)
        havekids = 1;
      p = p->next;
    }
    p = ptable.list[RUNNABLE].head;
    while(p){
      assertState(p, RUNNABLE);
      if(p->parent == curproc)
        havekids = 1;
      p = p->next;
    }
    p = ptable.list[RUNNING].head;
    while(p){
      assertState(p, RUNNING);
      if(p->parent == curproc)
        havekids = 1;
      p = p->next;
    }
    p = ptable.list[SLEEPING].head;
    while(p){
      assertState(p, SLEEPING);
      if(p->parent == curproc)
        havekids = 1;
      p = p->next;
    }
    p = ptable.list[ZOMBIE].head;
    while(p){
      if(p->parent == curproc){
        havekids = 1;
        int rc = stateListRemove(&ptable.list[ZOMBIE], p);
        if(rc == -1)
          panic("Error: not in zombie list");
        assertState(p, ZOMBIE);
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        stateListAdd(&ptable.list[UNUSED], p);
        release(&ptable.lock);
        return pid;
      }
      p = p->next;
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
#else
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
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
#endif // CS333_P3, Else

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifdef CS333_P3
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6
  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6

    acquire(&ptable.lock);
    // Get next process that is runnable
    p = ptable.list[RUNNABLE].head;
    if(p){
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      int rc = stateListRemove(&ptable.list[RUNNABLE], p);
      if(rc == -1)
        panic("Error: not in ready list");
      assertState(p, RUNNABLE);
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      stateListAdd(&ptable.list[RUNNING], p);
      p->cpu_ticks_in = ticks;
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#else
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
#ifdef CS333_P2
      p->cpu_ticks_in = ticks;
#endif // CS333_P2
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif // CS333_P3, Else
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
#ifdef CS333_P2
  p->cpu_ticks_total += ticks - p->cpu_ticks_in;
#endif // CS333_P2
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
#ifdef CS333_P3
  int rc = stateListRemove(&ptable.list[RUNNING], curproc);
  if(rc == -1)
    panic("Error: not in running list");
  assertState(curproc, RUNNING);
#endif // CS333_P3
  curproc->state = RUNNABLE;
#ifdef CS333_P3
  stateListAdd(&ptable.list[RUNNABLE], curproc);
#endif // CS333_P3
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

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  // Go to sleep.
#ifdef CS333_P3
  int rc = stateListRemove(&ptable.list[RUNNING], p);
  if(rc == -1)
    panic("Error: not in running list");
  assertState(p, RUNNING);
#endif
  p->chan = chan;
  p->state = SLEEPING;
#ifdef CS333_P3
  stateListAdd(&ptable.list[SLEEPING], p);
#endif

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
#ifdef CS333_P3
static void
wakeup1(void *chan)
{
  struct proc *next_p = 0, *p = ptable.list[SLEEPING].head;
  while(p){
    if(p->chan == chan){
      next_p = p->next;
      int rc = stateListRemove(&ptable.list[SLEEPING], p);
      if(rc == -1)
        panic("Error: not in sleep list");
      assertState(p, SLEEPING);
      p->state = RUNNABLE;
      stateListAdd(&ptable.list[RUNNABLE], p);
      p = next_p;
    } else
      p = p->next;
  }
}
#else
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#endif // CS333_P3

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
#ifdef CS333_P3
int
kill(int pid)
{
  struct proc *p;
  acquire(&ptable.lock);
  p = ptable.list[RUNNABLE].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p->next;
  }
  p = ptable.list[RUNNING].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p->next;
  }
  p = ptable.list[ZOMBIE].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p->next;
  }
  p = ptable.list[EMBRYO].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = p->next;
  }
  p = ptable.list[SLEEPING].head;
  while(p){
    if(p->pid == pid){
      p->killed = 1;
      int rc = stateListRemove(&ptable.list[SLEEPING], p);
      if(rc == -1)
        panic("Error: not in sleep list");
      assertState(p, SLEEPING);
      p->state = RUNNABLE;
      stateListAdd(&ptable.list[RUNNABLE], p);
      release(&ptable.lock);
      return 0;
    }
    p = p->next;
  }
  release(&ptable.lock);
  return -1;
}
#else
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
#endif // CS333_P3

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
#ifdef CS333_P1
  uint elapsed = 0;
  uint decimal = 0;
#endif // CS333_P1
#ifdef CS333_P2
  uint seconds = 0;
#endif // CS333_P2
#ifdef CS333_P2
  cprintf("\nPID\tName\tUID\tGID\tPPID\tElapsed\tCPU\tState\tSize\tPCs\n");
#elif CS333_P1
  cprintf("\nPID\tState\tName\tElapsed\t\tPCs\n");
#endif // CS333_P2, CS333_P1

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
#ifdef CS333_P2
    cprintf("%d\t%s\t%d\t%d", p->pid, p->name, p->uid, p->gid);
    if(!p->parent)
      cprintf("\t%d", p->pid);
    else
      cprintf("\t%d", p->parent->pid);
#elif CS333_P1
    cprintf("%d\t%s\t%s", p->pid, state, p->name);
#else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif // CS333_P2, CS333_P1, CS333_P0
#ifdef CS333_P1
    elapsed = ticks - p->start_ticks;
    decimal = elapsed % 1000;
    elapsed /= 1000;
    cprintf("\t%d.", elapsed);
    if(decimal < 10)
      cprintf("00%d", decimal);
    else if(decimal < 100)
      cprintf("0%d", decimal);
    else
      cprintf("%d", decimal);
#endif // CS333_P1
#ifdef CS333_P2
    elapsed = p->cpu_ticks_total;
    decimal = elapsed % 1000;
    seconds = elapsed / 1000;
    cprintf("\t%d.", seconds);
    if(decimal < 10)
      cprintf("00");
    else if(decimal < 100)
      cprintf("0");
    cprintf("%d\t%s\t%d", decimal, state, p->sz);
#endif // CS333_P2
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
#ifdef CS333_P1
        cprintf("\t%p", pc[i]);
#else
        cprintf(" %p", pc[i]);
#endif // CS333_P1, CS333_P0
    }
    cprintf("\n");
  }
}

#ifdef CS333_P2
int
getprocs(uint max, struct uproc* table)
{
  int i = 0;
  struct proc* p;
  acquire(&ptable.lock);
  if(!table || max <= 0){
    release(&ptable.lock);
    return -1;
  }
  for(p = ptable.proc;p < &ptable.proc[NPROC];p++){
    if(i >= max)
      break;
    if(p->state != EMBRYO && p->state != UNUSED){
      table[i].pid = p->pid;
      table[i].uid = p->uid;
      table[i].gid = p->gid;
      table[i].ppid = (!p->parent) ? p->pid:p->parent->pid;
      table[i].elapsed_ticks = ticks - p->start_ticks;
      table[i].CPU_total_ticks = p->cpu_ticks_total;
      table[i].size = p->sz;
      safestrcpy(table[i].state, states[p->state], sizeof(table[i]).state);
      safestrcpy(table[i].name, p->name, sizeof(table[i]).name);
      i++;
    }
  }
  release(&ptable.lock);
  return i;
}
#endif // CS333_P2

#ifdef CS333_P3
static void
stateListAdd(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL){
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  } else{
    ((*list).tail)->next = p;
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}

static int
stateListRemove(struct ptrs* list, struct proc* p)
{
    if(!(*list).head || !(*list).tail || !p){
      return -1;
    }
    struct proc* current = (*list).head;
    struct proc* previous = 0;

    if(current == p){
      (*list).head = ((*list).head)->next;
      // prevent tail remaining assigned when we've removed the only item
      // on the list
      if((*list).tail == p){
        (*list).tail = NULL;
      }
      return 0;
    }

    while(current){
      if(current == p){
        break;
      }
      previous = current;
      current = current->next;
    }

    // Process not found, hit eject.
    if(current == NULL){
      return -1;
    }

    // Process found. Set the appropriate next pointer
    if(current == (*list).tail){
      (*list).tail = previous;
      ((*list).tail)->next = NULL;
    } else {
      previous->next = current->next;
    }

    // Make sure p->next = doesn't point into the list.
    p->next = NULL;

    return 0;
}

static void
initProcessLists()
{
  int i;

  for (i = UNUSED; i <= ZOMBIE; i++) {
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
#ifdef CS333_P4
  for (i = 0; i <= MAXPRIO; i++) {
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
#endif // CS333_P4
}

static void
initFreeList(void)
{
  if(!holding(&ptable.lock))
    panic("acquire the ptable lock before calling initFreeList");

  struct proc* p;

  for (p = ptable.proc; p < ptable.proc + NPROC; ++p) {
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}

static void
assertState(struct proc* p, enum procstate state)
{
  if(p->state != state)
    panic("Not in certain state");
}

void
readydump(void)
{
  struct proc *current;
  acquire(&ptable.lock);
  cprintf("\nReady list processes:\n");
  current = ptable.list[RUNNABLE].head;
  if(!current)
    cprintf("None\n");
  else{
    while(current){
      cprintf("%d", current->pid);
      if(current->next)
        cprintf("->");
      else
        cprintf("\n");
      current = current->next;
    }
  }
  release(&ptable.lock);
}

void
freedump(void)
{
  int count = 0;
  struct proc *current;
  acquire(&ptable.lock);
  current = ptable.list[UNUSED].head;
  while(current){
    count++;
    current = current->next;
  }
  if(count == 1)
    cprintf("\nFree list size: %d process\n", count);
  else
    cprintf("\nFree list size: %d processes\n", count);
  release(&ptable.lock);
}

void
sleepdump(void)
{
  struct proc *current;
  acquire(&ptable.lock);
  current = ptable.list[SLEEPING].head;
  cprintf("\nSleep list processes:\n");
  if(!current)
    cprintf("None\n");
  else{
    while(current){
      cprintf("%d", current->pid);
      if(current->next)
        cprintf("->");
      else
        cprintf("\n");
      current = current->next;
    }
  }
  release(&ptable.lock);
}

void
zombiedump(void)
{
  int ppid;
  struct proc* current;
  acquire(&ptable.lock);
  current = ptable.list[ZOMBIE].head;
  cprintf("\nZombie list processes\n");
  if(!current)
    cprintf("None\n");
  else{
    while(current){
      ppid = (!current->parent) ? current->pid:current->parent->pid;
      cprintf("(%d,%d)", current->pid, ppid);
      if(current->next)
        cprintf("->");
      else
        cprintf("\n");
      current = current->next;
    }
  }
  release(&ptable.lock);
}
#endif // CS333_P3
