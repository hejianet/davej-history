/*
 * random.c -- A strong random number generator
 *
 * Version 0.95, last modified 4-Nov-95
 * 
 * Copyright Theodore Ts'o, 1994, 1995.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 * 
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * (now, with legal B.S. out of the way.....) 
 * 
 * This routine gathers environmental noise from device drivers, etc.,
 * and returns good random numbers, suitable for cryptographic use.
 * Besides the obvious cryptographic uses, these numbers are also good
 * for seeding TCP sequence numbers, and other places where it is
 * desireable to have numbers which are not only random, but hard to
 * predict by an attacker.
 *
 * Theory of operation
 * ===================
 * 
 * Computers are very predictable devices.  Hence it is extremely hard
 * to produce truely random numbers on a computer --- as opposed to
 * pseudo-random numbers, which can easily generated by using a
 * algorithm.  Unfortunately, it is very easy for attackers to guess
 * the sequence of pseudo-random number generators, and for some
 * applications this is not acceptable.  So instead, we must try to
 * gather "environmental noise" from the computer's environment, which
 * must be hard for outside attackers to observe, and use that to
 * generate random numbers.  In a Unix environment, this is best done
 * from inside the kernel.
 * 
 * Sources of randomness from the environment include inter-keyboard
 * timings, inter-interrupt timings from some interrupts, and other
 * events which are both (a) non-deterministic and (b) hard for an
 * outside observer to measure.  Randomness from these sources are
 * added to an "entropy pool", which is mixed using a CRC-like function.
 * This is not cryptographically strong, but it is adequate assuming
 * the randomness is not chosen maliciously, and it is fast enough that
 * the overhead of doing it on every interrupt is very reasonable.
 * As random bytes are mixed into the entropy pool, the routines keep
 * an *estimate* of how many bits of randomness have been stored into
 * the random number generator's internal state.
 * 
 * When random bytes are desired, they are obtained by taking the MD5
 * hash of the contents of the "entropy pool".  The MD5 hash avoids
 * exposing the internal state of the entropy pool.  It is believed to
 * be computationally infeasible to derive any useful information
 * about the input of MD5 from its output.  Even if it is possible to
 * analyze MD5 in some clever way, as long as the amount of data
 * returned from the generator is less than the inherent entropy in
 * the pool, the output data is totally unpredictable.  For this
 * reason, the routine decreases its internal estimate of how many
 * bits of "true randomness" are contained in the entropy pool as it
 * outputs random numbers.
 * 
 * If this estimate goes to zero, the routine can still generate
 * random numbers; however, an attacker may (at least in theory) be
 * able to infer the future output of the generator from prior
 * outputs.  This requires successful cryptanalysis of MD5, which is
 * not believed to be feasible, but there is a remote possiblility.
 * Nonetheless, these numbers should be useful for the vast majority
 * of purposes.
 * 
 * Exported interfaces ---- output
 * ===============================
 * 
 * There are three exported interfaces; the first is one designed to
 * be used from within the kernel:
 *
 * 	void get_random_bytes(void *buf, int nbytes);
 *
 * This interface will return the requested number of random bytes,
 * and place it in the requested buffer.
 * 
 * The two other interfaces are two character devices /dev/random and
 * /dev/urandom.  /dev/random is suitable for use when very high
 * quality randomness is desired (for example, for key generation or
 * one-time pads), as it will only return a maximum of the number of
 * bits of randomness (as estimated by the random number generator)
 * contained in the entropy pool.
 * 
 * The /dev/urandom device does not have this limit, and will return
 * as many bytes as are requested.  As more and more random bytes are
 * requested without giving time for the entropy pool to recharge,
 * this will result in random numbers that are merely cryptographically
 * strong.  For many applications, however, this is acceptable.
 *
 * Exported interfaces ---- input
 * ==============================
 * 
 * The current exported interfaces for gathering environmental noise
 * from the devices are:
 * 
 * 	void add_keyboard_randomness(unsigned char scancode);
 * 	void add_mouse_randomness(__u32 mouse_data);
 * 	void add_interrupt_randomness(int irq);
 * 	void add_blkdev_randomness(int irq);
 * 
 * add_keyboard_randomness() uses the inter-keypress timing, as well as the
 * scancode as random inputs into the "entropy pool".
 * 
 * add_mouse_randomness() uses the mouse interrupt timing, as well as
 * the reported position of the mouse from the hardware.
 *
 * add_interrupt_randomness() uses the inter-interrupt timing as random
 * inputs to the entropy pool.  Note that not all interrupts are good
 * sources of randomness!  For example, the timer interrupts is not a
 * good choice, because the periodicity of the interrupts is to
 * regular, and hence predictable to an attacker.  Disk interrupts are
 * a better measure, since the timing of the disk interrupts are more
 * unpredictable.
 * 
 * add_blkdev_randomness() times the finishing time of block requests.
 * 
 * All of these routines try to estimate how many bits of randomness a
 * particular randomness source.  They do this by keeping track of the
 * first and second order deltas of the event timings.
 *
 * Acknowledgements:
 * =================
 *
 * Ideas for constructing this random number generator were derived
 * from the Pretty Good Privacy's random number generator, and from
 * private discussions with Phil Karn.  Colin Plumb provided a faster
 * random number generator, which speed up the mixing function of the
 * entropy pool, taken from PGP 3.0 (under development).  It has since
 * been modified by myself to provide better mixing in the case where
 * the input values to add_entropy_word() are mostly small numbers.
 * 
 * Any flaws in the design are solely my responsibility, and should
 * not be attributed to the Phil, Colin, or any of authors of PGP.
 * 
 * The code for MD5 transform was taken from Colin Plumb's
 * implementation, which has been placed in the public domain.  The
 * MD5 cryptographic checksum was devised by Ronald Rivest, and is
 * documented in RFC 1321, "The MD5 Message Digest Algorithm".
 * 
 * Further background information on this topic may be obtained from
 * RFC 1750, "Randomness Recommendations for Security", by Donald
 * Eastlake, Steve Crocker, and Jeff Schiller.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>
#include <linux/random.h>

#include <asm/segment.h>
#include <asm/irq.h>
#include <asm/io.h>

/*
 * The pool is stirred with a primitive polynomial of degree 128
 * over GF(2), namely x^128 + x^99 + x^59 + x^31 + x^9 + x^7 + 1.
 * For a pool of size 64, try x^64+x^62+x^38+x^10+x^6+x+1.
 */
#define POOLWORDS 128    /* Power of 2 - note that this is 32-bit words */
#define POOLBITS (POOLWORDS*32)
#if POOLWORDS == 128
#define TAP1    99     /* The polynomial taps */
#define TAP2    59
#define TAP3    31
#define TAP4    9
#define TAP5    7
#elif POOLWORDS == 64
#define TAP1    62      /* The polynomial taps */
#define TAP2    38
#define TAP3    10
#define TAP4    6
#define TAP5    1
#else
#error No primitive polynomial available for chosen POOLWORDS
#endif

/* There is actually only one of these, globally. */
struct random_bucket {
	unsigned add_ptr;
	unsigned entropy_count;
	int input_rotate;
	__u32 *pool;
};

/* There is one of these per entropy source */
struct timer_rand_state {
	unsigned long	last_time;
	int 		last_delta;
	int		dont_count_entropy:1;
};

static struct random_bucket random_state;
static __u32 random_pool[POOLWORDS];
static struct timer_rand_state keyboard_timer_state;
static struct timer_rand_state mouse_timer_state;
static struct timer_rand_state extract_timer_state;
static struct timer_rand_state *irq_timer_state[NR_IRQS];
static struct timer_rand_state *blkdev_timer_state[MAX_BLKDEV];
static struct wait_queue *random_wait;

static int random_read(struct inode * inode, struct file * file,
		       char * buf, int nbytes);
static int random_read_unlimited(struct inode * inode, struct file * file,
				 char * buf, int nbytes);
static int random_select(struct inode *inode, struct file *file,
			 int sel_type, select_table * wait);
static int random_write(struct inode * inode, struct file * file,
			const char * buffer, int count);
static int random_ioctl(struct inode * inode, struct file * file,
			unsigned int cmd, unsigned long arg);


#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif
	
void rand_initialize(void)
{
	random_state.add_ptr = 0;
	random_state.entropy_count = 0;
	random_state.pool = random_pool;
	memset(irq_timer_state, 0, sizeof(irq_timer_state));
	memset(blkdev_timer_state, 0, sizeof(blkdev_timer_state));
	extract_timer_state.dont_count_entropy = 1;
	random_wait = NULL;
}

void rand_initialize_irq(int irq)
{
	struct timer_rand_state *state;
	
	if (irq >= NR_IRQS || irq_timer_state[irq])
		return;

	/*
	 * If kamlloc returns null, we just won't use that entropy
	 * source.
	 */
	state = kmalloc(sizeof(struct timer_rand_state), GFP_KERNEL);
	if (state) {
		irq_timer_state[irq] = state;
		memset(state, 0, sizeof(struct timer_rand_state));
	}
}

void rand_initialize_blkdev(int major)
{
	struct timer_rand_state *state;
	
	if (major >= MAX_BLKDEV || blkdev_timer_state[major])
		return;

	/*
	 * If kamlloc returns null, we just won't use that entropy
	 * source.
	 */
	state = kmalloc(sizeof(struct timer_rand_state), GFP_KERNEL);
	if (state) {
		blkdev_timer_state[major] = state;
		memset(state, 0, sizeof(struct timer_rand_state));
	}
}

/*
 * This function adds a byte into the entropy "pool".  It does not
 * update the entropy estimate.  The caller must do this if appropriate.
 *
 * The pool is stirred with a primitive polynomial of degree 128
 * over GF(2), namely x^128 + x^99 + x^59 + x^31 + x^9 + x^7 + 1.
 * For a pool of size 64, try x^64+x^62+x^38+x^10+x^6+x+1.
 * 
 * We rotate the input word by a changing number of bits, to help
 * assure that all bits in the entropy get toggled.  Otherwise, if we
 * consistently feed the entropy pool small numbers (like jiffies and
 * scancodes, for example), the upper bits of the entropy pool don't
 * get affected. --- TYT, 10/11/95
 */
static inline void add_entropy_word(struct random_bucket *r,
				    const __u32 input)
{
	unsigned i;
	__u32 w;

	w = (input << r->input_rotate) | (input >> (32 - r->input_rotate));
	i = r->add_ptr = (r->add_ptr - 1) & (POOLWORDS-1);
	if (i)
		r->input_rotate = (r->input_rotate + 7) & 31;
	else
		/*
		 * At the beginning of the pool, add an extra 7 bits
		 * rotation, so that successive passes spread the
		 * input bits across the pool evenly.
		 */
		r->input_rotate = (r->input_rotate + 14) & 31;

	/* XOR in the various taps */
	w ^= r->pool[(i+TAP1)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP2)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP3)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP4)&(POOLWORDS-1)];
	w ^= r->pool[(i+TAP5)&(POOLWORDS-1)];
	w ^= r->pool[i];
	/* Rotate w left 1 bit (stolen from SHA) and store */
	r->pool[i] = (w << 1) | (w >> 31);
}

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays.  It uses the timer_rand_state structure to make an estimate
 * of how many bits of entropy this call has added to the pool.
 *
 * The number "num" is also added to the pool - it should somehow describe
 * the type of event which just happened.  This is currently 0-255 for
 * keyboard scan codes, and 256 upwards for interrupts.
 * On the i386, this is assumed to be at most 16 bits, and the high bits
 * are used for a high-resolution timer.
 *
 * TODO: Read the time stamp register on the Pentium.
 */
static void add_timer_randomness(struct random_bucket *r,
				 struct timer_rand_state *state, unsigned num)
{
	int	delta, delta2;
	unsigned	nbits;
	__u32		time;

#if defined (__i386__)
	if (x86_capability & 16) {
		unsigned long low, high;
		__asm__(".byte 0x0f,0x31"
			:"=a" (low), "=d" (high));
		time = (__u32) low;
		num ^= (__u32) high;
	} else {
#if 0
		/*
		 * On a 386, read the high resolution timer.  We assume that
		 * this gives us 2 bits of randomness.
		 *
		 * This is turned off for now because of the speed hit
		 * it entails.
		 */ 
		outb_p(0x00, 0x43);	/* latch the count ASAP */
		num |= inb_p(0x40) << 16;
		num |= inb(0x40) << 24;
		if (!state->dont_count_entropy)
			r->entropy_count += 2;
#endif
		
		time = jiffies;
	}
#else
	time = jiffies;
#endif

	add_entropy_word(r, (__u32) num);
	add_entropy_word(r, time);

	/*
	 * Calculate number of bits of randomness we probably
	 * added.  We take into account the first and second order
	 * deltas in order to make our estimate.
	 */
	if (!state->dont_count_entropy) {
		delta = time - state->last_time;
		state->last_time = time;

		delta2 = delta - state->last_delta;
		state->last_delta = delta;

		if (delta < 0) delta = -delta;
		if (delta2 < 0) delta2 = -delta2;
		delta = MIN(delta, delta2) >> 1;
		for (nbits = 0; delta; nbits++)
			delta >>= 1;

		r->entropy_count += nbits;
	
		/* Prevent overflow */
		if (r->entropy_count > POOLBITS)
			r->entropy_count = POOLBITS;
	}
		
	wake_up_interruptible(&random_wait);	
}

void add_keyboard_randomness(unsigned char scancode)
{
	add_timer_randomness(&random_state, &keyboard_timer_state, scancode);
}

void add_mouse_randomness(__u32 mouse_data)
{
	add_timer_randomness(&random_state, &mouse_timer_state, mouse_data);
}

void add_interrupt_randomness(int irq)
{
	if (irq >= NR_IRQS || irq_timer_state[irq] == 0)
		return;

	add_timer_randomness(&random_state, irq_timer_state[irq], 0x100+irq);
}

void add_blkdev_randomness(int major)
{
	if (major >= MAX_BLKDEV || blkdev_timer_state[major] == 0)
		return;

	add_timer_randomness(&random_state, blkdev_timer_state[major],
			     0x200+major);
}

/*
 * MD5 transform algorithm, taken from code written by Colin Plumb,
 * and put into the public domain
 *
 * QUESTION: Replace this with SHA, which as generally received better
 * reviews from the cryptographic community?
 */

/* The four core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
	( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x )

/*
 * The core of the MD5 algorithm, this alters an existing MD5 hash to
 * reflect the addition of 16 longwords of new data.  MD5Update blocks
 * the data and converts bytes into longwords for this routine.
 */
static void MD5Transform(__u32 buf[4],
			 __u32 const in[16])
{
	__u32 a, b, c, d;

	a = buf[0];
	b = buf[1];
	c = buf[2];
	d = buf[3];

	MD5STEP(F1, a, b, c, d, in[ 0]+0xd76aa478,  7);
	MD5STEP(F1, d, a, b, c, in[ 1]+0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, in[ 2]+0x242070db, 17);
	MD5STEP(F1, b, c, d, a, in[ 3]+0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, in[ 4]+0xf57c0faf,  7);
	MD5STEP(F1, d, a, b, c, in[ 5]+0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, in[ 6]+0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, in[ 7]+0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, in[ 8]+0x698098d8,  7);
	MD5STEP(F1, d, a, b, c, in[ 9]+0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, in[10]+0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, in[11]+0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, in[12]+0x6b901122,  7);
	MD5STEP(F1, d, a, b, c, in[13]+0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, in[14]+0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, in[15]+0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, in[ 1]+0xf61e2562,  5);
	MD5STEP(F2, d, a, b, c, in[ 6]+0xc040b340,  9);
	MD5STEP(F2, c, d, a, b, in[11]+0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, in[ 0]+0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, in[ 5]+0xd62f105d,  5);
	MD5STEP(F2, d, a, b, c, in[10]+0x02441453,  9);
	MD5STEP(F2, c, d, a, b, in[15]+0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, in[ 4]+0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, in[ 9]+0x21e1cde6,  5);
	MD5STEP(F2, d, a, b, c, in[14]+0xc33707d6,  9);
	MD5STEP(F2, c, d, a, b, in[ 3]+0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, in[ 8]+0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, in[13]+0xa9e3e905,  5);
	MD5STEP(F2, d, a, b, c, in[ 2]+0xfcefa3f8,  9);
	MD5STEP(F2, c, d, a, b, in[ 7]+0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, in[12]+0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, in[ 5]+0xfffa3942,  4);
	MD5STEP(F3, d, a, b, c, in[ 8]+0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, in[11]+0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, in[14]+0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, in[ 1]+0xa4beea44,  4);
	MD5STEP(F3, d, a, b, c, in[ 4]+0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, in[ 7]+0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, in[10]+0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, in[13]+0x289b7ec6,  4);
	MD5STEP(F3, d, a, b, c, in[ 0]+0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, in[ 3]+0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, in[ 6]+0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, in[ 9]+0xd9d4d039,  4);
	MD5STEP(F3, d, a, b, c, in[12]+0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, in[15]+0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, in[ 2]+0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, in[ 0]+0xf4292244,  6);
	MD5STEP(F4, d, a, b, c, in[ 7]+0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, in[14]+0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, in[ 5]+0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, in[12]+0x655b59c3,  6);
	MD5STEP(F4, d, a, b, c, in[ 3]+0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, in[10]+0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, in[ 1]+0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, in[ 8]+0x6fa87e4f,  6);
	MD5STEP(F4, d, a, b, c, in[15]+0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, in[ 6]+0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, in[13]+0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, in[ 4]+0xf7537e82,  6);
	MD5STEP(F4, d, a, b, c, in[11]+0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, in[ 2]+0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, in[ 9]+0xeb86d391, 21);

	buf[0] += a;
	buf[1] += b;
	buf[2] += c;
	buf[3] += d;
}

#undef F1
#undef F2
#undef F3
#undef F4
#undef MD5STEP


#if POOLWORDS % 16
#error extract_entropy() assumes that POOLWORDS is a multiple of 16 words.
#endif
/*
 * This function extracts randomness from the "entropy pool", and
 * returns it in a buffer.  This function computes how many remaining
 * bits of entropy are left in the pool, but it does not restrict the
 * number of bytes that are actually obtained.
 */
static inline int extract_entropy(struct random_bucket *r, char * buf,
				  int nbytes, int to_user)
{
	int ret, i;
	__u32 tmp[4];
	
	add_timer_randomness(r, &extract_timer_state, nbytes);
	
	/* Redundant, but just in case... */
	if (r->entropy_count > POOLBITS) 
		r->entropy_count = POOLBITS;
	/* Why is this here?  Left in from Ted Ts'o.  Perhaps to limit time. */
	if (nbytes > 32768)
		nbytes = 32768;

	ret = nbytes;
	if (r->entropy_count / 8 >= nbytes)
		r->entropy_count -= nbytes*8;
	else
		r->entropy_count = 0;

	while (nbytes) {
		/* Hash the pool to get the output */
		tmp[0] = 0x67452301;
		tmp[1] = 0xefcdab89;
		tmp[2] = 0x98badcfe;
		tmp[3] = 0x10325476;
		for (i = 0; i < POOLWORDS; i += 16)
			MD5Transform(tmp, r->pool+i);
		/* Modify pool so next hash will produce different results */
		add_entropy_word(r, tmp[0]);
		add_entropy_word(r, tmp[1]);
		add_entropy_word(r, tmp[2]);
		add_entropy_word(r, tmp[3]);
		/*
		 * Run the MD5 Transform one more time, since we want
		 * to add at least minimal obscuring of the inputs to
		 * add_entropy_word().  --- TYT
		 */
		MD5Transform(tmp, r->pool);
		
		/* Copy data to destination buffer */
		i = MIN(nbytes, 16);
		if (to_user)
			memcpy_tofs(buf, (__u8 const *)tmp, i);
		else
			memcpy(buf, (__u8 const *)tmp, i);
		nbytes -= i;
		buf += i;
	}

	/* Wipe data from memory */
	memset(tmp, 0, sizeof(tmp));
	
	return ret;
}

/*
 * This function is the exported kernel interface.  It returns some
 * number of good random numbers, suitable for seeding TCP sequence
 * numbers, etc.
 */
void get_random_bytes(void *buf, int nbytes)
{
	extract_entropy(&random_state, (char *) buf, nbytes, 0);
}

static int
random_read(struct inode * inode, struct file * file, char * buf, int nbytes)
{
	struct wait_queue 	wait = { current, NULL };
	int			n;
	int			retval = 0;
	int			count = 0;
	
	if (nbytes == 0)
		return 0;

	add_wait_queue(&random_wait, &wait);
	while (nbytes > 0) {
		current->state = TASK_INTERRUPTIBLE;
		
		n = nbytes;
		if (n > random_state.entropy_count / 8)
			n = random_state.entropy_count / 8;
		if (n == 0) {
			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (current->signal & ~current->blocked) {
				retval = -ERESTARTSYS;
				break;
			}
			schedule();
			continue;
		}
		n = extract_entropy(&random_state, buf, n, 1);
		count += n;
		buf += n;
		nbytes -= n;
		break;		/* This break makes the device work */
				/* like a named pipe */
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&random_wait, &wait);

	return (count ? count : retval);
}

static int
random_read_unlimited(struct inode * inode, struct file * file,
		      char * buf, int nbytes)
{
	return extract_entropy(&random_state, buf, nbytes, 1);
}

static int
random_select(struct inode *inode, struct file *file,
		      int sel_type, select_table * wait)
{
	if (sel_type == SEL_IN) {
		if (random_state.entropy_count >= 8)
			return 1;
		select_wait(&random_wait, wait);
	}
	return 0;
}

static int
random_write(struct inode * inode, struct file * file,
	     const char * buffer, int count)
{
	int i;
	__u32 word, *p;

	for (i = count, p = (__u32 *)buffer;
	     i >= sizeof(__u32);
	     i-= sizeof(__u32), p++) {
		memcpy_fromfs(&word, p, sizeof(__u32));
		add_entropy_word(&random_state, word);
	}
	if (i) {
		word = 0;
		memcpy_fromfs(&word, p, i);
		add_entropy_word(&random_state, word);
	}
	if (inode)
		inode->i_mtime = CURRENT_TIME;
	return count;
}

static int
random_ioctl(struct inode * inode, struct file * file,
	     unsigned int cmd, unsigned long arg)
{
	int *p, size, ent_count;
	int retval;
	
	switch (cmd) {
	case RNDGETENTCNT:
		retval = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
		if (retval)
			return(retval);
		put_user(random_state.entropy_count, (int *) arg);
		return 0;
	case RNDADDTOENTCNT:
		if (!suser())
			return -EPERM;
		retval = verify_area(VERIFY_READ, (void *) arg, sizeof(int));
		if (retval)
			return(retval);
		random_state.entropy_count += get_user((int *) arg);
		if (random_state.entropy_count > POOLBITS)
			random_state.entropy_count = POOLBITS;
		return 0;
	case RNDGETPOOL:
		if (!suser())
			return -EPERM;
		p = (int *) arg;
		retval = verify_area(VERIFY_WRITE, (void *) p, sizeof(int));
		if (retval)
			return(retval);
		put_user(random_state.entropy_count, p++);
		retval = verify_area(VERIFY_READ, (void *) p, sizeof(int));
		if (retval)
			return(retval);
		size = get_user(p);
		put_user(POOLWORDS, p);
		if (size < 0)
			return -EINVAL;
		if (size > POOLWORDS)
			size = POOLWORDS;
		memcpy_tofs(++p, random_state.pool,
			    size*sizeof(__u32));
		return 0;
	case RNDADDENTROPY:
		if (!suser())
			return -EPERM;
		p = (int *) arg;
		retval = verify_area(VERIFY_READ, (void *) p, 2*sizeof(int));
		if (retval)
			return(retval);
		ent_count = get_user(p++);
		size = get_user(p++);
		(void) random_write(0, file, (const char *) p, size);
		random_state.entropy_count += ent_count;
		if (random_state.entropy_count > POOLBITS)
			random_state.entropy_count = POOLBITS;
		return 0;
	case RNDZAPENTCNT:
		if (!suser())
			return -EPERM;
		random_state.entropy_count = 0;
		return 0;
	default:
		return -EINVAL;
	}
}

struct file_operations random_fops = {
	NULL,		/* random_lseek */
	random_read,
	random_write,
	NULL,		/* random_readdir */
	random_select,	/* random_select */
	random_ioctl,
	NULL,		/* random_mmap */
	NULL,		/* no special open code */
	NULL		/* no special release code */
};

struct file_operations urandom_fops = {
	NULL,		/* unrandom_lseek */
	random_read_unlimited,
	random_write,
	NULL,		/* urandom_readdir */
	NULL,		/* urandom_select */
	random_ioctl,
	NULL,		/* urandom_mmap */
	NULL,		/* no special open code */
	NULL		/* no special release code */
};
