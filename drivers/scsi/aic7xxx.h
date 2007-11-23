/* @(#)aic7xxx.h 1.14 94/11/30 jda */

/*
 * Adaptec 274x/284x/294x device driver for Linux.
 * Copyright (c) 1994 The University of Calgary Department of Computer Science.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef aic7xxx_h
#define aic7xxx_h

#define AIC7XXX_H_VERSION	"1.14"

/*
 *  Scsi_Host_Template (see hosts.h) for 274x - some fields
 *  to do with card config are filled in after the card is
 *  detected.
 */
#define AIC7XXX	{						\
	NULL,							\
	NULL,							\
	NULL,							\
	aic7xxx_detect,						\
	NULL,							\
	aic7xxx_info,						\
	NULL,							\
	aic7xxx_queue,						\
	aic7xxx_abort,						\
	aic7xxx_reset,						\
	NULL,							\
	aic7xxx_biosparam,					\
	-1,			/* max simultaneous cmds      */\
	-1,			/* scsi id of host adapter    */\
	SG_ALL,			/* max scatter-gather cmds    */\
	1,			/* cmds per lun (linked cmds) */\
	0,			/* number of 274x's present   */\
	0,			/* no memory DMA restrictions */\
	DISABLE_CLUSTERING					\
}

extern int aic7xxx_queue(Scsi_Cmnd *, void (*)(Scsi_Cmnd *));
extern int aic7xxx_biosparam(Disk *, int, int[]);
extern int aic7xxx_detect(Scsi_Host_Template *);
extern int aic7xxx_command(Scsi_Cmnd *);
extern int aic7xxx_abort(Scsi_Cmnd *);
extern int aic7xxx_reset(Scsi_Cmnd *);

extern const char *aic7xxx_info(struct Scsi_Host *);

#endif