/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  Derived from "arch/alpha/kernel/setup.c"
 *    Copyright (C) 1995 Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/ide.h>
#include <asm/pci-bridge.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/pmu.h>
#include <asm/mediabay.h>
#include <asm/ohare.h>
#include <asm/mediabay.h>
#include "time.h"

extern int root_mountflags;

unsigned char drive_info;

#define DEFAULT_ROOT_DEVICE 0x0801	/* sda1 - slightly silly choice */

extern void zs_kgdb_hook(int tty_num);
static void ohare_init(void);

__pmac

int
pmac_get_cpuinfo(char *buffer)
{
	int len;
	/* should find motherboard type here as well */
	len = sprintf(buffer,"machine\t\t: PowerMac\n");
	return len;
}

#ifdef CONFIG_SCSI
/* Find the device number for the disk (if any) at target tgt
   on host adaptor host.
   XXX this really really should be in drivers/scsi/sd.c. */
#include <linux/blkdev.h>
#include "../../../drivers/scsi/scsi.h"
#include "../../../drivers/scsi/sd.h"
#include "../../../drivers/scsi/hosts.h"

kdev_t sd_find_target(void *host, int tgt)
{
    Scsi_Disk *dp;
    int i;

    for (dp = rscsi_disks, i = 0; i < sd_template.dev_max; ++i, ++dp)
        if (dp->device != NULL && dp->device->host == host
            && dp->device->id == tgt)
            return MKDEV(SCSI_DISK_MAJOR, i << 4);
    return 0;
}
#endif

__initfunc(void
pmac_setup_arch(unsigned long *memory_start_p, unsigned long *memory_end_p))
{
	struct device_node *cpu;
	int *fp;

	/* Set loops_per_sec to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0) {
			switch (_get_PVR() >> 16) {
			case 4:		/* 604 */
			case 9:		/* 604e */
			case 10:	/* mach V (604ev5) */
			case 20:	/* 620 */
				loops_per_sec = *fp;
				break;
			default:	/* 601, 603, etc. */
				loops_per_sec = *fp / 2;
			}
		} else
			loops_per_sec = 50000000;
	}

	/* this area has the CPU identification register
	   and some registers used by smp boards */
	ioremap(0xf8000000, 0x1000);

	*memory_start_p = pmac_find_bridges(*memory_start_p, *memory_end_p);

	ohare_init();

#ifdef CONFIG_KGDB
	zs_kgdb_hook(0);
#endif

	find_via_cuda();
	find_via_pmu();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
#ifdef CONFIG_ABSCON_COMPAT
	/* Console wrapper */
	conswitchp = &compat_con;
#endif

	kd_mksound = pmac_mksound;
}

static volatile u32 *feature_addr;

__initfunc(static void ohare_init(void))
{
	struct device_node *np;

	np = find_devices("ohare");
	if (np == 0)
		return;
	if (np->next != 0)
		printk(KERN_WARNING "only using the first ohare\n");
	if (np->n_addrs == 0) {
		printk(KERN_ERR "No addresses for %s\n", np->full_name);
		return;
	}
	feature_addr = (volatile u32 *)
		ioremap(np->addrs[0].address + OHARE_FEATURE_REG, 4);

	if (find_devices("via-pmu") == 0) {
		printk(KERN_INFO "Twiddling the magic ohare bits\n");
		out_le32(feature_addr, STARMAX_FEATURES);
	} else {
		out_le32(feature_addr, in_le32(feature_addr) | PBOOK_FEATURES);
		printk(KERN_DEBUG "feature reg = %x\n", in_le32(feature_addr));
	}
}

extern char *bootpath;
extern char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
kdev_t boot_dev;

__initfunc(void powermac_init(void))
{
	adb_init();
	pmac_nvram_init();
	if (_machine == _MACH_Pmac) {
		media_bay_init();
	}
#ifdef CONFIG_PMAC_CONSOLE
	pmac_find_display();
#endif
}

__initfunc(void
note_scsi_host(struct device_node *node, void *host))
{
	int l;
	char *p;

	l = strlen(node->full_name);
	if (bootpath != NULL && bootdevice != NULL
	    && strncmp(node->full_name, bootdevice, l) == 0
	    && (bootdevice[l] == '/' || bootdevice[l] == 0)) {
		boot_host = host;
		/*
		 * There's a bug in OF 1.0.5.  (Why am I not surprised.)
		 * If you pass a path like scsi/sd@1:0 to canon, it returns
		 * something like /bandit@F2000000/gc@10/53c94@10000/sd@0,0
		 * That is, the scsi target number doesn't get preserved.
		 * So we pick the target number out of bootpath and use that.
		 */
		p = strstr(bootpath, "/sd@");
		if (p != NULL) {
			p += 4;
			boot_target = simple_strtoul(p, NULL, 10);
			p = strchr(p, ':');
			if (p != NULL)
				boot_part = simple_strtoul(p + 1, NULL, 10);
		}
	}
}

__initfunc(void find_boot_device(void))
{
	kdev_t dev;

	if (kdev_t_to_nr(ROOT_DEV) != 0)
		return;
	ROOT_DEV = to_kdev_t(DEFAULT_ROOT_DEVICE);
	if (boot_host == NULL)
		return;
#ifdef CONFIG_SCSI
	dev = sd_find_target(boot_host, boot_target);
	if (dev == 0)
		return;
	boot_dev = MKDEV(MAJOR(dev), MINOR(dev) + boot_part);
#endif
	/* XXX should cope with booting from IDE also */
}

__initfunc(void note_bootable_part(kdev_t dev, int part))
{
	static int found_boot = 0;

	if (!found_boot) {
		find_boot_device();
		found_boot = 1;
	}
	if (dev == boot_dev) {
		ROOT_DEV = MKDEV(MAJOR(dev), MINOR(dev) + part);
		boot_dev = NODEV;
		printk(" (root)");
	}
}

#ifdef CONFIG_BLK_DEV_IDE
int pmac_ide_ports_known;
ide_ioreg_t pmac_ide_regbase[MAX_HWIFS];
int pmac_ide_irq[MAX_HWIFS];

__initfunc(void pmac_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq))
{
	int i;

	*p = 0;
	if (base == 0)
		return;
	if (base == mb_cd_base && !check_media_bay(MB_CD)) {
		mb_cd_index = -1;
		return;
	}
	for (i = 0; i < 8; ++i)
		*p++ = base + i * 0x10;
	*p = base + 0x160;
	if (irq != NULL) {
		*irq = 0;
		for (i = 0; i < MAX_HWIFS; ++i) {
			if (base == pmac_ide_regbase[i]) {
				*irq = pmac_ide_irq[i];
				break;
			}
		}
	}
}

__initfunc(void pmac_ide_probe(void))
{
	struct device_node *np;
	int i;
	struct device_node *atas;
	struct device_node *p, **pp, *removables, **rp;

	pp = &atas;
	rp = &removables;
	p = find_devices("ATA");
	if (p == NULL)
		p = find_devices("IDE");
	/* Move removable devices such as the media-bay CDROM
	   on the PB3400 to the end of the list. */
	for (; p != NULL; p = p->next) {
		if (p->parent && p->parent->name
		    && strcasecmp(p->parent->name, "media-bay") == 0) {
			*rp = p;
			rp = &p->next;
		} else {
			*pp = p;
			pp = &p->next;
		}
	}
	*rp = NULL;
	*pp = removables;

	for (i = 0, np = atas; i < MAX_HWIFS && np != NULL; np = np->next) {
		if (np->n_addrs == 0) {
			printk(KERN_WARNING "ide: no address for device %s\n",
			       np->full_name);
			continue;
		}
		pmac_ide_regbase[i] = (unsigned long)
			ioremap(np->addrs[0].address, 0x200);
		if (np->n_intrs == 0) {
			printk("ide: no intrs for device %s, using 13\n",
			       np->full_name);
			pmac_ide_irq[i] = 13;
		} else {
			pmac_ide_irq[i] = np->intrs[0].line;
		}

		if (np->parent && np->parent->name
		    && strcasecmp(np->parent->name, "media-bay") == 0) {
			mb_cd_index = i;
			mb_cd_base = pmac_ide_regbase[i];
			mb_cd_irq = pmac_ide_irq[i];
		}

		++i;
	}

	pmac_ide_ports_known = 1;
}
#endif /* CONFIG_BLK_DEV_IDE */
