/*
 * net/dst.h	Protocol independent destination cache definitions.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#ifndef _NET_DST_H
#define _NET_DST_H

/*
 * 0 - no debugging messages
 * 1 - rare events and bugs (default)
 * 2 - trace mode.
 */
#ifdef  NO_ANK_FIX
#define RT_CACHE_DEBUG		0
#else
#define RT_CACHE_DEBUG		1
#endif

#define DST_GC_MIN	(1*HZ)
#define DST_GC_INC	(5*HZ)
#define DST_GC_MAX	(120*HZ)

struct sk_buff;

struct dst_entry
{
	struct dst_entry        *next;
	atomic_t		refcnt;
	atomic_t		use;
	struct device	        *dev;
	char			obsolete;
	char			priority;
	char			__pad1, __pad2;
	unsigned long		lastuse;
	unsigned		window;
	unsigned		pmtu;
	unsigned		rtt;
	int			error;

	struct dst_entry	*neighbour;
	struct hh_cache		*hh;

	int			(*input)(struct sk_buff*);
	int			(*output)(struct sk_buff*);

	struct  dst_ops	        *ops;
	
	char			info[0];
};


struct dst_ops
{
	unsigned short		family;
	struct dst_entry *	(*check)(struct dst_entry *);
	struct dst_entry *	(*reroute)(struct dst_entry *);
	void			(*destroy)(struct dst_entry *);
};

extern struct dst_entry * dst_garbage_list;
extern atomic_t	dst_total;

static __inline__
struct dst_entry * dst_clone(struct dst_entry * dst)
{
	if (dst)
		atomic_inc(&dst->refcnt);
	return dst;
}

static __inline__
void dst_release(struct dst_entry * dst)
{
	if (dst)
		atomic_dec(&dst->refcnt);
}

static __inline__
struct dst_entry * dst_check(struct dst_entry ** dst_p)
{
	struct dst_entry * dst = *dst_p;
	if (dst && dst->obsolete)
		dst = dst->ops->check(dst);
	return (*dst_p = dst);
}

static __inline__
struct dst_entry * dst_reroute(struct dst_entry ** dst_p)
{
	struct dst_entry * dst = *dst_p;
	if (dst && dst->obsolete)
		dst = dst->ops->reroute(dst);
	return (*dst_p = dst);
}

static __inline__
void dst_destroy(struct dst_entry * dst)
{
	if (dst->neighbour)
		dst_release(dst->neighbour);
	if (dst->ops->destroy)
		dst->ops->destroy(dst);
	kfree(dst);
	atomic_dec(&dst_total);
}

extern void * dst_alloc(int size, struct dst_ops * ops);
extern void __dst_free(struct dst_entry * dst);

static __inline__
void dst_free(struct dst_entry * dst)
{
	if (!dst->refcnt) {
		dst_destroy(dst);
		return;
	}
	__dst_free(dst);
}

#endif /* _NET_DST_H */