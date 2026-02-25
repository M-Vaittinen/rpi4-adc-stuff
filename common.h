#ifndef MVA_COMMON_H
#define MVA_COMMON_H

//#define BE_LAZY

#define MAX_SAMPS	1024
//#define MAX_SAMPS	32
#define BUFF_MASK	0x1FFF
//#define BUFF_MASK	0xFF
//#define NUM_DATA_CHUNKS 0x1FFF 
#define NUM_DATA_CHUNKS 0x2000 /* Power of 2 */
//#define NUM_DATA_CHUNKS 0xFF /* Power of 2 */

#if (NUM_DATA_CHUNKS & BUFF_MASK)
#error "NUM_DATA_CHUNKS must be power of two"
#endif

#ifdef BE_LAZY

/* SPINAWHILE: CPU pause/yield instruction during busy-wait loops
 * Purpose: Reduce power consumption and bus contention when spinning
 * - On x86: 'pause' reduces pipeline flushes on spin loops
 * - On ARM v7+: 'yield' hints CPU can switch to another thread
 * - Fallback: memory barrier to prevent over-optimization
 */
#if defined(__i386__) || defined(__x86_64__)
	#define SPINAWHILE() __asm__ __volatile__("pause" ::: "memory");

#elif defined(__aarch64__) || defined(__arm__)
	#if defined(__aarch64__) || (defined(__ARM_ARCH) && __ARM_ARCH >= 7)
		#define SPINAWHILE() __asm__ __volatile__("yield" ::: "memory");
	#else
		#define SPINAWHILE() __asm__ __volatile__("" ::: "memory");
	#endif
#else
	#define SPINAWHILE() __asm__ __volatile__("" ::: "memory");
#endif

#else // Don't be lazy but use tight busy loop
	/* SPINAWHILE: Yield CPU to scheduler during busy-wait
	 * Uses sched_yield() to allow other processes to run
	 */
	#define SPINAWHILE() sched_yield()
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_arr) (sizeof(_arr)/sizeof(_arr[0]))
#endif

#endif
