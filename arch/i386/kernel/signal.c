/*
 *  linux/arch/i386/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>

#include <asm/uaccess.h>

#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);

asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, unsigned long set)
{
	struct pt_regs * regs = (struct pt_regs *) &restart;
	unsigned long mask;

	spin_lock_irq(&current->sigmask_lock);
	mask = current->blocked;
	current->blocked = set & _BLOCKABLE;
	spin_unlock_irq(&current->sigmask_lock);

	regs->eax = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(mask, regs))
			return -EINTR;
	}
}

static inline void restore_i387_hard(struct _fpstate *buf)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		stts();
	}
#else
	if (current == last_task_used_math) {
		last_task_used_math = NULL;
		stts();
	}
#endif
	current->used_math = 1;
	current->flags &= ~PF_USEDFPU;
	copy_from_user(&current->tss.i387.hard, buf, sizeof(*buf));
}

static void restore_i387(struct _fpstate *buf)
{
#ifndef CONFIG_MATH_EMULATION
	restore_i387_hard(buf);
#else
	if (hard_math) {
		restore_i387_hard(buf);
		return;
	}
	restore_i387_soft(buf);
#endif	
}
	

/*
 * This sets regs->esp even though we don't actually use sigstacks yet..
 */
asmlinkage int sys_sigreturn(unsigned long __unused)
{
#define COPY(x) regs->x = context->x
#define COPY_SEG(seg) \
{ unsigned int tmp = context->seg; \
if (   (tmp & 0xfffc)     /* not a NULL selectors */ \
    && (tmp & 0x4) != 0x4 /* not a LDT selector */ \
    && (tmp & 3) != 3     /* not a RPL3 GDT selector */ \
   ) goto badframe; \
regs->x##seg = tmp; }
#define COPY_SEG_STRICT(seg) \
{ unsigned int tmp = context->seg; \
if ((tmp & 0xfffc) && (tmp & 3) != 3) goto badframe; \
regs->x##seg = tmp; }
#define GET_SEG(seg) \
{ unsigned int tmp = context->seg; \
if (   (tmp & 0xfffc)     /* not a NULL selectors */ \
    && (tmp & 0x4) != 0x4 /* not a LDT selector */ \
    && (tmp & 3) != 3     /* not a RPL3 GDT selector */ \
   ) goto badframe; \
__asm__("mov %w0,%%" #seg: :"r" (tmp)); }
	struct sigcontext * context;
	struct pt_regs * regs;

	regs = (struct pt_regs *) &__unused;
	context = (struct sigcontext *) regs->esp;
	if (verify_area(VERIFY_READ, context, sizeof(*context)))
		goto badframe;
	current->blocked = context->oldmask & _BLOCKABLE;
	COPY_SEG(ds);
	COPY_SEG(es);
	GET_SEG(fs);
	GET_SEG(gs);
	COPY_SEG_STRICT(ss);
	COPY_SEG_STRICT(cs);
	COPY(eip);
	COPY(ecx); COPY(edx);
	COPY(ebx);
	COPY(esp); COPY(ebp);
	COPY(edi); COPY(esi);
	regs->eflags &= ~0x40DD5;
	regs->eflags |= context->eflags & 0x40DD5;
	regs->orig_eax = -1;		/* disable syscall checks */
	if (context->fpstate) {
		struct _fpstate * buf = context->fpstate;
		if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
			goto badframe;
		restore_i387(buf);
	}
	return context->eax;

badframe:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

static inline struct _fpstate * save_i387_hard(struct _fpstate * buf)
{
#ifdef __SMP__
	if (current->flags & PF_USEDFPU) {
		__asm__ __volatile__("fnsave %0":"=m" (current->tss.i387.hard));
		stts();
		current->flags &= ~PF_USEDFPU;
	}
#else
	if (current == last_task_used_math) {
		__asm__ __volatile__("fnsave %0":"=m" (current->tss.i387.hard));
		last_task_used_math = NULL;
		__asm__ __volatile__("fwait");	/* not needed on 486+ */
		stts();
	}
#endif
	current->tss.i387.hard.status = current->tss.i387.hard.swd;
	copy_to_user(buf, &current->tss.i387.hard, sizeof(*buf));
	current->used_math = 0;
	return buf;
}

static struct _fpstate * save_i387(struct _fpstate * buf)
{
	if (!current->used_math)
		return NULL;

#ifndef CONFIG_MATH_EMULATION
	return save_i387_hard(buf);
#else
	if (hard_math)
		return save_i387_hard(buf);
	return save_i387_soft(buf);
#endif
}

/*
 * Set up a signal frame... Make the stack look the way iBCS2 expects
 * it to look.
 */
static void setup_frame(struct sigaction * sa,
	struct pt_regs * regs, int signr,
	unsigned long oldmask)
{
	unsigned long * frame;

	frame = (unsigned long *) regs->esp;
	if ((regs->xss & 0xffff) != USER_DS && sa->sa_restorer)
		frame = (unsigned long *) sa->sa_restorer;
	frame -= 64;
	if (!access_ok(VERIFY_WRITE,frame,64*4))
		goto segv_and_exit;

/* set up the "normal" stack seen by the signal handler (iBCS2) */
#define __CODE ((unsigned long)(frame+24))
#define CODE(x) ((unsigned long *) ((x)+__CODE))
	
    /* XXX Can possible miss a SIGSEGV when frame crosses a page border
       and a thread unmaps it while we are accessing it. 
       So either check all put_user() calls or don't do it at all.  
       We use __put_user() here because the access_ok() call was already
       done earlier. */  
	if (__put_user(__CODE,frame))
		goto segv_and_exit;
	if (current->exec_domain && current->exec_domain->signal_invmap)
		__put_user(current->exec_domain->signal_invmap[signr], frame+1);
	else
		__put_user(signr, frame+1);
	{
		unsigned int tmp = 0;
#define PUT_SEG(seg, mem) \
__asm__("mov %%" #seg",%w0":"=r" (tmp):"0" (tmp)); __put_user(tmp,mem);
		PUT_SEG(gs, frame+2);
		PUT_SEG(fs, frame+3);
	}
	__put_user(regs->xes, frame+4);
	__put_user(regs->xds, frame+5);
	__put_user(regs->edi, frame+6);
	__put_user(regs->esi, frame+7);
	__put_user(regs->ebp, frame+8);
	__put_user(regs->esp, frame+9);
	__put_user(regs->ebx, frame+10);
	__put_user(regs->edx, frame+11);
	__put_user(regs->ecx, frame+12);
	__put_user(regs->eax, frame+13);
	__put_user(current->tss.trap_no, frame+14);
	__put_user(current->tss.error_code, frame+15);
	__put_user(regs->eip, frame+16);
	__put_user(regs->xcs, frame+17);
	__put_user(regs->eflags, frame+18);
	__put_user(regs->esp, frame+19);
	__put_user(regs->xss, frame+20);
	__put_user((unsigned long) save_i387((struct _fpstate *)(frame+32)),frame+21);
/* non-iBCS2 extensions.. */
	__put_user(oldmask, frame+22);
	__put_user(current->tss.cr2, frame+23);
/* set up the return code... */
	__put_user(0x0000b858, CODE(0));	/* popl %eax ; movl $,%eax */
	__put_user(0x80cd0000, CODE(4));	/* int $0x80 */
	__put_user(__NR_sigreturn, CODE(2));
#undef __CODE
#undef CODE

	/* Set up registers for signal handler */
	regs->esp = (unsigned long) frame;
	regs->eip = (unsigned long) sa->sa_handler;
	{
		unsigned long seg = USER_DS;
		__asm__("mov %w0,%%fs ; mov %w0,%%gs":"=r" (seg) :"0" (seg));
		set_fs(seg);
		regs->xds = seg;
		regs->xes = seg;
		regs->xss = seg;
		regs->xcs = USER_CS;
	}
	regs->eflags &= ~TF_MASK;
	return;

segv_and_exit:
	lock_kernel();
	do_exit(SIGSEGV);
	unlock_kernel();
}

/*
 * OK, we're invoking a handler
 */	
static void handle_signal(unsigned long signr, struct sigaction *sa,
	unsigned long oldmask, struct pt_regs * regs)
{
	/* are we from a system call? */
	if (regs->orig_eax >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->eax) {
			case -ERESTARTNOHAND:
				regs->eax = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(sa->sa_flags & SA_RESTART)) {
					regs->eax = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->eax = regs->orig_eax;
				regs->eip -= 2;
		}
	}

	/* set up the stack frame */
	setup_frame(sa, regs, signr, oldmask);

	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	if (!(sa->sa_flags & SA_NOMASK)) {
		spin_lock_irq(&current->sigmask_lock);
		current->blocked |= (sa->sa_mask | _S(signr)) & _BLOCKABLE;
		spin_unlock_irq(&current->sigmask_lock);
	}
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
asmlinkage int do_signal(unsigned long oldmask, struct pt_regs * regs)
{
	unsigned long mask;
	unsigned long signr;
	struct sigaction * sa;

	mask = ~current->blocked;
	while ((signr = current->signal & mask)) {
		/*
		 *	This stops gcc flipping out. Otherwise the assembler
		 *	including volatiles for the inline function to get
		 *	current combined with this gets it confused.
		 */
		struct task_struct *t=current;
		__asm__("bsf %3,%1\n\t"
#ifdef __SMP__
			"lock ; "
#endif
			"btrl %1,%0"
			:"=m" (t->signal),"=r" (signr)
			:"0" (t->signal), "1" (signr));
		sa = current->sig->action + signr;
		signr++;
		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current);
			schedule();
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;
			if (signr == SIGSTOP)
				continue;
			if (_S(signr) & current->blocked) {
				spin_lock_irq(&current->sigmask_lock);
				current->signal |= _S(signr);
				spin_unlock_irq(&current->sigmask_lock);
				continue;
			}
			sa = current->sig->action + signr - 1;
		}
		if (sa->sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* check for SIGCHLD: it's special */
			while (sys_wait4(-1,NULL,WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}
		if (sa->sa_handler == SIG_DFL) {
			if (current->pid == 1)
				continue;
			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
			case SIGSTOP:
				if (current->flags & PF_PTRACED)
					continue;
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if (!(current->p_pptr->sig->action[SIGCHLD-1].sa_flags & 
						SA_NOCLDSTOP))
					notify_parent(current);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
				if (current->binfmt && current->binfmt->core_dump) {
					if (current->binfmt->core_dump(signr, regs))
						signr |= 0x80;
				}
				/* fall through */
			default:
				spin_lock_irq(&current->sigmask_lock);
				current->signal |= _S(signr & 0x7f);
				spin_unlock_irq(&current->sigmask_lock);

				current->flags |= PF_SIGNALED;

				lock_kernel(); /* 8-( */
				do_exit(signr);
				unlock_kernel();
			}
		}
		handle_signal(signr, sa, oldmask, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (regs->orig_eax >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->eax == -ERESTARTNOHAND ||
		    regs->eax == -ERESTARTSYS ||
		    regs->eax == -ERESTARTNOINTR) {
			regs->eax = regs->orig_eax;
			regs->eip -= 2;
		}
	}
	return 0;
}
