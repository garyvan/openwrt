/*
 *  arch/arm/include/asm/atomic.h
 *
 *  Copyright (C) 1996 Russell King.
 *  Copyright (C) 2002 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_ATOMIC_H
#define __ASM_ARM_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/irqflags.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#ifdef CONFIG_GENERIC_ATOMIC64
#include <asm-generic/atomic64.h>
#endif

#define ATOMIC_INIT(i)	{ (i) }

#ifdef __KERNEL__

#define _ASM_EXTABLE(from, to)		\
"	.pushsection __ex_table,\"a\"\n"\
"	.align	3\n"			\
"	.long	" #from ", " #to"\n"	\
"	.popsection"

/*
 * On ARM, ordinary assignment (str instruction) doesn't clear the local
 * strex/ldrex monitor on some implementations. The reason we can use it for
 * atomic_set() is the clrex or dummy strex done on every exception return.
 */
#define atomic_read(v)	(*(volatile int *)&(v)->counter)
static inline int atomic_read_unchecked(const atomic_unchecked_t *v)
{
	return v->counter;
}
#define atomic_set(v,i)	(((v)->counter) = (i))
static inline void atomic_set_unchecked(atomic_unchecked_t *v, int i)
{
	v->counter = i;
}

#if __LINUX_ARM_ARCH__ >= 6

/*
 * ARMv6 UP and SMP safe atomic ops.  We use load exclusive and
 * store exclusive to ensure that these are atomic.  We may loop
 * to ensure that the update happens.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	__asm__ __volatile__("@ atomic_add\n"
"1:	ldrex	%1, [%3]\n"
"	adds	%0, %1, %4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");
}

static inline void atomic_add_unchecked(int i, atomic_unchecked_t *v)
{
	unsigned long tmp;
	int result;

	__asm__ __volatile__("@ atomic_add_unchecked\n"
"1:	ldrex	%0, [%3]\n"
"	add	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");
}

static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	smp_mb();

	__asm__ __volatile__("@ atomic_add_return\n"
"1:	ldrex	%1, [%3]\n"
"	adds	%0, %1, %4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"	mov	%0, %1\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline int atomic_add_return_unchecked(int i, atomic_unchecked_t *v)
{
	unsigned long tmp;
	int result;

	smp_mb();

	__asm__ __volatile__("@ atomic_add_return_unchecked\n"
"1:	ldrex	%0, [%3]\n"
"	add	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline void atomic_sub(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	__asm__ __volatile__("@ atomic_sub\n"
"1:	ldrex	%1, [%3]\n"
"	subs	%0, %1, %4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");
}

static inline void atomic_sub_unchecked(int i, atomic_unchecked_t *v)
{
	unsigned long tmp;
	int result;

	__asm__ __volatile__("@ atomic_sub_unchecked\n"
"1:	ldrex	%0, [%3]\n"
"	sub	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	smp_mb();

	__asm__ __volatile__("@ atomic_sub_return\n"
"1:	ldrex	%1, [%3]\n"
"	subs	%0, %1, %4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"	mov	%0, %1\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline int atomic_cmpxchg(atomic_t *ptr, int old, int new)
{
	unsigned long oldval, res;

	smp_mb();

	do {
		__asm__ __volatile__("@ atomic_cmpxchg\n"
		"ldrex	%1, [%3]\n"
		"mov	%0, #0\n"
		"teq	%1, %4\n"
		"strexeq %0, %5, [%3]\n"
		    : "=&r" (res), "=&r" (oldval), "+Qo" (ptr->counter)
		    : "r" (&ptr->counter), "Ir" (old), "r" (new)
		    : "cc");
	} while (res);

	smp_mb();

	return oldval;
}

static inline int atomic_cmpxchg_unchecked(atomic_unchecked_t *ptr, int old, int new)
{
	unsigned long oldval, res;

	smp_mb();

	do {
		__asm__ __volatile__("@ atomic_cmpxchg_unchecked\n"
		"ldrex	%1, [%3]\n"
		"mov	%0, #0\n"
		"teq	%1, %4\n"
		"strexeq %0, %5, [%3]\n"
		    : "=&r" (res), "=&r" (oldval), "+Qo" (ptr->counter)
		    : "r" (&ptr->counter), "Ir" (old), "r" (new)
		    : "cc");
	} while (res);

	smp_mb();

	return oldval;
}

static inline void atomic_clear_mask(unsigned long mask, unsigned long *addr)
{
	unsigned long tmp, tmp2;

	__asm__ __volatile__("@ atomic_clear_mask\n"
"1:	ldrex	%0, [%3]\n"
"	bic	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (tmp), "=&r" (tmp2), "+Qo" (*addr)
	: "r" (addr), "Ir" (mask)
	: "cc");
}

#else /* ARM_ARCH_6 */

#ifdef CONFIG_SMP
#error SMP not supported on pre-ARMv6 CPUs
#endif

static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long flags;
	int val;

	raw_local_irq_save(flags);
	val = v->counter;
	v->counter = val += i;
	raw_local_irq_restore(flags);

	return val;
}

static inline int atomic_add_return_unchecked(int i, atomic_unchecked_t *v)
{
	return atomic_add_return(i, v);
}

#define atomic_add(i, v)	(void) atomic_add_return(i, v)
static inline void atomic_add_unchecked(int i, atomic_unchecked_t *v)
{
	(void) atomic_add_return(i, v);
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long flags;
	int val;

	raw_local_irq_save(flags);
	val = v->counter;
	v->counter = val -= i;
	raw_local_irq_restore(flags);

	return val;
}
#define atomic_sub(i, v)	(void) atomic_sub_return(i, v)
static inline void atomic_sub_unchecked(int i, atomic_unchecked_t *v)
{
	(void) atomic_sub_return(i, v);
}

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	raw_local_irq_save(flags);
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;
	raw_local_irq_restore(flags);

	return ret;
}

static inline int atomic_cmpxchg_unchecked(atomic_unchecked_t *v, int old, int new)
{
	return atomic_cmpxchg(v, old, new);
}

static inline void atomic_clear_mask(unsigned long mask, unsigned long *addr)
{
	unsigned long flags;

	raw_local_irq_save(flags);
	*addr &= ~mask;
	raw_local_irq_restore(flags);
}

#endif /* __LINUX_ARM_ARCH__ */

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))
static inline int atomic_xchg_unchecked(atomic_unchecked_t *v, int new)
{
	return xchg(&v->counter, new);
}

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;

	c = atomic_read(v);
	while (c != u && (old = atomic_cmpxchg((v), c, c + a)) != c)
		c = old;
	return c;
}

#define atomic_inc(v)		atomic_add(1, v)
static inline void atomic_inc_unchecked(atomic_unchecked_t *v)
{
	atomic_add_unchecked(1, v);
}
#define atomic_dec(v)		atomic_sub(1, v)
static inline void atomic_dec_unchecked(atomic_unchecked_t *v)
{
	atomic_sub_unchecked(1, v);
}

#define atomic_inc_and_test(v)	(atomic_add_return(1, v) == 0)
static inline int atomic_inc_and_test_unchecked(atomic_unchecked_t *v)
{
	return atomic_add_return_unchecked(1, v) == 0;
}
#define atomic_dec_and_test(v)	(atomic_sub_return(1, v) == 0)
#define atomic_inc_return(v)    (atomic_add_return(1, v))
static inline int atomic_inc_return_unchecked(atomic_unchecked_t *v)
{
	return atomic_add_return_unchecked(1, v);
}
#define atomic_dec_return(v)    (atomic_sub_return(1, v))
#define atomic_sub_and_test(i, v) (atomic_sub_return(i, v) == 0)

#define atomic_add_negative(i,v) (atomic_add_return(i, v) < 0)

#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__after_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_inc()	smp_mb()

#ifndef CONFIG_GENERIC_ATOMIC64
typedef struct {
	u64 __aligned(8) counter;
} atomic64_t;

#ifdef CONFIG_PAX_REFCOUNT
typedef struct {
	u64 __aligned(8) counter;
} atomic64_unchecked_t;
#else
typedef atomic64_t atomic64_unchecked_t;
#endif

#define ATOMIC64_INIT(i) { (i) }

#ifdef CONFIG_ARM_LPAE
static inline u64 atomic64_read(const atomic64_t *v)
{
	u64 result;

	__asm__ __volatile__("@ atomic64_read\n"
"	ldrd	%0, %H0, [%1]"
	: "=&r" (result)
	: "r" (&v->counter), "Qo" (v->counter)
	);

	return result;
}

static inline u64 atomic64_read_unchecked(const atomic64_unchecked_t *v)
{
	u64 result;

	__asm__ __volatile__("@ atomic64_read_unchecked\n"
"	ldrd	%0, %H0, [%1]"
	: "=&r" (result)
	: "r" (&v->counter), "Qo" (v->counter)
	);

	return result;
}

static inline void atomic64_set(atomic64_t *v, u64 i)
{
	__asm__ __volatile__("@ atomic64_set\n"
"	strd	%2, %H2, [%1]"
	: "=Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	);
}

static inline void atomic64_set_unchecked(atomic64_unchecked_t *v, u64 i)
{
	__asm__ __volatile__("@ atomic64_set_unchecked\n"
"	strd	%2, %H2, [%1]"
	: "=Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	);
}
#else
static inline u64 atomic64_read(const atomic64_t *v)
{
	u64 result;

	__asm__ __volatile__("@ atomic64_read\n"
"	ldrexd	%0, %H0, [%1]"
	: "=&r" (result)
	: "r" (&v->counter), "Qo" (v->counter)
	);

	return result;
}

static inline u64 atomic64_read_unchecked(atomic64_unchecked_t *v)
{
	u64 result;

	__asm__ __volatile__("@ atomic64_read_unchecked\n"
"	ldrexd	%0, %H0, [%1]"
	: "=&r" (result)
	: "r" (&v->counter), "Qo" (v->counter)
	);

	return result;
}

static inline void atomic64_set(atomic64_t *v, u64 i)
{
	u64 tmp;

	__asm__ __volatile__("@ atomic64_set\n"
"1:	ldrexd	%0, %H0, [%2]\n"
"	strexd	%0, %3, %H3, [%2]\n"
"	teq	%0, #0\n"
"	bne	1b"
	: "=&r" (tmp), "=Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static inline void atomic64_set_unchecked(atomic64_unchecked_t *v, u64 i)
{
	u64 tmp;

	__asm__ __volatile__("@ atomic64_set_unchecked\n"
"1:	ldrexd	%0, %H0, [%2]\n"
"	strexd	%0, %3, %H3, [%2]\n"
"	teq	%0, #0\n"
"	bne	1b"
	: "=&r" (tmp), "=Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

#endif

static inline void atomic64_add(u64 i, atomic64_t *v)
{
	u64 result;
	unsigned long tmp;

	__asm__ __volatile__("@ atomic64_add\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	adds	%Q0, %Q0, %Q4\n"
"	adcs	%R0, %R0, %R4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static inline void atomic64_add_unchecked(u64 i, atomic64_unchecked_t *v)
{
	u64 result;
	unsigned long tmp;

	__asm__ __volatile__("@ atomic64_add_unchecked\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	adds	%Q0, %Q0, %Q4\n"
"	adc	%R0, %R0, %R4\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static inline u64 atomic64_add_return(u64 i, atomic64_t *v)
{
	u64 result, tmp;

	smp_mb();

	__asm__ __volatile__("@ atomic64_add_return\n"
"1:	ldrexd	%1, %H1, [%3]\n"
"	adds	%Q0, %Q1, %Q4\n"
"	adcs	%R0, %R1, %R4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"	mov	%Q0, %Q1\n"
"	mov	%R0, %R1\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline u64 atomic64_add_return_unchecked(u64 i, atomic64_unchecked_t *v)
{
	u64 result;
	unsigned long tmp;

	smp_mb();

	__asm__ __volatile__("@ atomic64_add_return_unchecked\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	adds	%Q0, %Q0, %Q4\n"
"	adc	%R0, %R0, %R4\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline void atomic64_sub(u64 i, atomic64_t *v)
{
	u64 result;
	unsigned long tmp;

	__asm__ __volatile__("@ atomic64_sub\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	subs	%Q0, %Q0, %Q4\n"
"	sbcs	%R0, %R0, %R4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static inline void atomic64_sub_unchecked(u64 i, atomic64_unchecked_t *v)
{
	u64 result;
	unsigned long tmp;

	__asm__ __volatile__("@ atomic64_sub_unchecked\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	subs	%Q0, %Q0, %Q4\n"
"	sbc	%R0, %R0, %R4\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static inline u64 atomic64_sub_return(u64 i, atomic64_t *v)
{
	u64 result, tmp;

	smp_mb();

	__asm__ __volatile__("@ atomic64_sub_return\n"
"1:	ldrexd	%1, %H1, [%3]\n"
"	subs	%Q0, %Q1, %Q4\n"
"	sbcs	%R0, %R1, %R4\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"	mov	%Q0, %Q1\n"
"	mov	%R0, %R1\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"

#ifdef CONFIG_PAX_REFCOUNT
"\n4:\n"
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline u64 atomic64_cmpxchg(atomic64_t *ptr, u64 old, u64 new)
{
	u64 oldval;
	unsigned long res;

	smp_mb();

	do {
		__asm__ __volatile__("@ atomic64_cmpxchg\n"
		"ldrexd		%1, %H1, [%3]\n"
		"mov		%0, #0\n"
		"teq		%1, %4\n"
		"teqeq		%H1, %H4\n"
		"strexdeq	%0, %5, %H5, [%3]"
		: "=&r" (res), "=&r" (oldval), "+Qo" (ptr->counter)
		: "r" (&ptr->counter), "r" (old), "r" (new)
		: "cc");
	} while (res);

	smp_mb();

	return oldval;
}

static inline u64 atomic64_cmpxchg_unchecked(atomic64_unchecked_t *ptr, u64 old, u64 new)
{
	u64 oldval;
	unsigned long res;

	smp_mb();

	do {
		__asm__ __volatile__("@ atomic64_cmpxchg_unchecked\n"
		"ldrexd		%1, %H1, [%3]\n"
		"mov		%0, #0\n"
		"teq		%1, %4\n"
		"teqeq		%H1, %H4\n"
		"strexdeq	%0, %5, %H5, [%3]"
		: "=&r" (res), "=&r" (oldval), "+Qo" (ptr->counter)
		: "r" (&ptr->counter), "r" (old), "r" (new)
		: "cc");
	} while (res);

	smp_mb();

	return oldval;
}

static inline u64 atomic64_xchg(atomic64_t *ptr, u64 new)
{
	u64 result;
	unsigned long tmp;

	smp_mb();

	__asm__ __volatile__("@ atomic64_xchg\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	strexd	%1, %4, %H4, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (ptr->counter)
	: "r" (&ptr->counter), "r" (new)
	: "cc");

	smp_mb();

	return result;
}

static inline u64 atomic64_dec_if_positive(atomic64_t *v)
{
	u64 result, tmp;

	smp_mb();

	__asm__ __volatile__("@ atomic64_dec_if_positive\n"
"1:	ldrexd	%1, %H1, [%3]\n"
"	subs	%Q0, %Q1, #1\n"
"	sbcs	%R0, %R1, #0\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"	mov	%Q0, %Q1\n"
"	mov	%R0, %R1\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	teq	%R0, #0\n"
"	bmi	4f\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b\n"
"4:\n"

#ifdef CONFIG_PAX_REFCOUNT
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter)
	: "cc");

	smp_mb();

	return result;
}

static inline int atomic64_add_unless(atomic64_t *v, u64 a, u64 u)
{
	u64 val;
	unsigned long tmp;
	int ret = 1;

	smp_mb();

	__asm__ __volatile__("@ atomic64_add_unless\n"
"1:	ldrexd	%0, %H0, [%4]\n"
"	teq	%0, %5\n"
"	teqeq	%H0, %H5\n"
"	moveq	%1, #0\n"
"	beq	4f\n"
"	adds	%Q0, %Q0, %Q6\n"
"	adcs	%R0, %R0, %R6\n"

#ifdef CONFIG_PAX_REFCOUNT
"	bvc	3f\n"
"2:	bkpt	0xf103\n"
"3:\n"
#endif

"	strexd	%2, %0, %H0, [%4]\n"
"	teq	%2, #0\n"
"	bne	1b\n"
"4:\n"

#ifdef CONFIG_PAX_REFCOUNT
	_ASM_EXTABLE(2b, 4b)
#endif

	: "=&r" (val), "+r" (ret), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (u), "r" (a)
	: "cc");

	if (ret)
		smp_mb();

	return ret;
}

#define atomic64_add_negative(a, v)	(atomic64_add_return((a), (v)) < 0)
#define atomic64_inc(v)			atomic64_add(1LL, (v))
#define atomic64_inc_unchecked(v)	atomic64_add_unchecked(1LL, (v))
#define atomic64_inc_return(v)		atomic64_add_return(1LL, (v))
#define atomic64_inc_return_unchecked(v)	atomic64_add_return_unchecked(1LL, (v))
#define atomic64_inc_and_test(v)	(atomic64_inc_return(v) == 0)
#define atomic64_sub_and_test(a, v)	(atomic64_sub_return((a), (v)) == 0)
#define atomic64_dec(v)			atomic64_sub(1LL, (v))
#define atomic64_dec_unchecked(v)	atomic64_sub_unchecked(1LL, (v))
#define atomic64_dec_return(v)		atomic64_sub_return(1LL, (v))
#define atomic64_dec_and_test(v)	(atomic64_dec_return((v)) == 0)
#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1LL, 0LL)

#endif /* !CONFIG_GENERIC_ATOMIC64 */
#endif
#endif
