/*
 *  linux/arch/arm/kernel/irq.c
 *
 *  Copyright (C) 1992 Linus Torvalds
 *  Modifications for ARM processor Copyright (C) 1995-1998 Russell King.
 *  FIQ support written by Philip Blundell <philb@gnu.org>, 1998.
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */
#include <linux/config.h> /* for CONFIG_DEBUG_ERRORS */
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/fiq.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/arch/irq.h>

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];
spinlock_t irq_controller_lock;
static struct fiq_handler *current_fiq;
static unsigned long no_fiq_insn;

#define FIQ_VECTOR ((unsigned long *)0x1c)

#ifndef SMP
#define irq_enter(cpu, irq)	(++local_irq_count[cpu])
#define irq_exit(cpu, irq)	(--local_irq_count[cpu])
#else
#error SMP not supported
#endif

#ifdef CONFIG_ARCH_ACORN
/* Bitmask indicating valid interrupt numbers
 * (to be moved to include/asm-arm/arch-*)
 */
unsigned long validirqs[NR_IRQS / 32] = {
	0x003ffe7f,	0x000001ff,	0x000000ff,	0x00000000
};

#define valid_irq(x) ((x) < NR_IRQS && validirqs[(x) >> 5] & (1 << ((x) & 31)))
#else

#define valid_irq(x) ((x) < NR_IRQS)
#endif

void disable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
#ifdef cliIF
	save_flags(flags);
	cliIF();
#endif
	mask_irq(irq_nr);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

void enable_irq(unsigned int irq_nr)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
#ifdef cliIF
	save_flags (flags);
	cliIF();
#endif
	unmask_irq(irq_nr);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

struct irqaction *irq_action[NR_IRQS];

int get_irq_list(char *buf)
{
	int i;
	struct irqaction * action;
	char *p = buf;

	for (i = 0 ; i < NR_IRQS ; i++) {
	    	action = irq_action[i];
		if (!action)
			continue;
		p += sprintf(p, "%3d: %10u   %s",
			     i, kstat_irqs(i), action->name);
		for (action = action->next; action; action = action->next) {
			p += sprintf(p, ", %s", action->name);
		}
		*p++ = '\n';
	}
	p += sprintf(p, "FIQ:              %s\n",
		     current_fiq?current_fiq->name:"unused");
	return p - buf;
}

/*
 * do_IRQ handles all normal device IRQ's
 */
asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	int status, cpu;

#if defined(HAS_IOMD) || defined(HAS_IOC)
	if (irq != IRQ_EXPANSIONCARD)
#endif
	{
		spin_lock(&irq_controller_lock);
		mask_and_ack_irq(irq);
		spin_unlock(&irq_controller_lock);
	}

	cpu = smp_processor_id();
	irq_enter(cpu, irq);
	kstat.irqs[cpu][irq]++;

	/* Return with this interrupt masked if no action */
	status = 0;
	action = *(irq + irq_action);
	if (action) {
		if (!(action->flags & SA_INTERRUPT))
			__sti();

		do {
			status |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
		if (status & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();

		switch (irq) {
#if defined(HAS_IOMD) || defined(HAS_IOC)
		case IRQ_KEYBOARDTX:
		case IRQ_EXPANSIONCARD:
			break;
#endif
#ifdef HAS_IOMD
		case IRQ_DMA0:
		case IRQ_DMA1:
		case IRQ_DMA2:
		case IRQ_DMA3:
			break;
#endif

		default:
			spin_lock(&irq_controller_lock);
			unmask_irq(irq);
			spin_unlock(&irq_controller_lock);
			break;
		}
	}

	irq_exit(cpu, irq);
	/*
	 * This should be conditional: we should really get
	 * a return code from the irq handler to tell us
	 * whether the handler wants us to do software bottom
	 * half handling or not..
	 *
	 * ** IMPORTANT NOTE: do_bottom_half() ENABLES IRQS!!! **
	 * **  WE MUST DISABLE THEM AGAIN, ELSE IDE DISKS GO   **
	 * **                       AWOL                       **
	 */
	if (1) {
		if (bh_active & bh_mask)
			do_bottom_half();
		__cli();
	}
}

#if defined(CONFIG_ARCH_ACORN)
void do_ecard_IRQ(int irq, struct pt_regs *regs)
{
	struct irqaction * action;

	action = *(irq + irq_action);
	if (action) {
		do {
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
	} else {
		spin_lock(&irq_controller_lock);
		mask_irq (irq);
		spin_unlock(&irq_controller_lock);
	}
}
#endif

int setup_arm_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	p = irq_action + irq;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ))
			return -EBUSY;

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	if (new->flags & SA_SAMPLE_RANDOM)
	        rand_initialize_irq(irq);

	save_flags_cli(flags);
	*p = new;

	if (!shared) {
		spin_lock(&irq_controller_lock);
		unmask_irq(irq);
		spin_unlock(&irq_controller_lock);
	}
	restore_flags(flags);
	return 0;
}

/*
 * Using "struct sigaction" is slightly silly, but there
 * are historical reasons and it works well, so..
 */
int request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		 unsigned long irq_flags, const char * devname, void *dev_id)
{
	unsigned long retval;
	struct irqaction *action;
        
	if (!valid_irq(irq))
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irq_flags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_arm_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (!valid_irq(irq)) {
		printk(KERN_ERR "Trying to free IRQ%d\n",irq);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
		return;
	}
	for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

	    	/* Found it - now free it */
		save_flags_cli (flags);
		*p = action->next;
		restore_flags (flags);
		kfree(action);
		return;
	}
	printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
#ifdef CONFIG_DEBUG_ERRORS
	__backtrace();
#endif
}

unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0;
	unsigned long delay;

	/* first snaffle up any unassigned irqs */
	for (i = 15; i > 0; i--) {
		if (!irq_action[i] && valid_irq(i)) {
			enable_irq(i);
			irqs |= 1 << i;
		}
	}

	/* wait for spurious interrupts to mask themselves out again */
	for (delay = jiffies + HZ/10; delay > jiffies; )
		/* min 100ms delay */;

	/* now filter out any obviously spurious interrupts */
	return irqs & get_enabled_irqs();
}

int probe_irq_off (unsigned long irqs)
{
	unsigned int i;

	irqs &= ~get_enabled_irqs();
	if (!irqs)
		return 0;
	i = ffz (~irqs);
	if (irqs != (irqs & (1 << i)))
		i = -i;
	return i;
}

int claim_fiq(struct fiq_handler *f)
{
	if (current_fiq) {
		if (current_fiq->callback == NULL || (*current_fiq->callback)())
			return -EBUSY;
	}
	current_fiq = f;
	return 0;
}

void release_fiq(struct fiq_handler *f)
{
	if (current_fiq != f) {
		printk(KERN_ERR "%s tried to release FIQ when not owner!\n",
		       f->name);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
		return;
	}
	current_fiq = NULL;

	*FIQ_VECTOR = no_fiq_insn;
	__flush_entry_to_ram(FIQ_VECTOR);
}

__initfunc(void init_IRQ(void))
{
	extern void init_dma(void);

	irq_init_irq();

	current_fiq = NULL;
	no_fiq_insn = *FIQ_VECTOR;

	init_dma();
}
