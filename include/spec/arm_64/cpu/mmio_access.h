/*
 * \brief  Access to memory-mapped registers by single load/store instructions
 * \author Ivan Loskutov
 * \date   2026-10-09
 *
 * Each function executes exactly one load or store instruction of the width
 * of the accessed type, written in assembly. In contrast to an access via a
 * volatile pointer, the compiler can neither choose the instruction and its
 * addressing mode, nor split, merge, or widen the access. That is what a
 * device register needs, and what a trapped access relies on.
 *
 * The functions are meant for code that accesses registers at addresses
 * known at run time only, and for code that runs before Genode's MMIO
 * framework ('util/mmio.h') can be used. Registers with a fixed layout are
 * better described by the framework.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__SPEC__ARM_64__CPU__MMIO_ACCESS_H_
#define _INCLUDE__SPEC__ARM_64__CPU__MMIO_ACCESS_H_

#include <base/stdint.h>

namespace Hw {

	/**
	 * Read a register of type T (unsigned integer of 8, 16, 32, or 64 bit)
	 */
	template <typename T> inline T mmio_read(Genode::addr_t addr);

	/**
	 * Write a register of type T (unsigned integer of 8, 16, 32, or 64 bit)
	 */
	template <typename T> inline void mmio_write(Genode::addr_t addr, T value);
}


template <typename T>
inline T Hw::mmio_read(Genode::addr_t const addr)
{
	static_assert(T(-1) > T(0), "register type must be unsigned");

	if constexpr (sizeof(T) == 8) {
		Genode::uint64_t value;
		asm volatile ("ldr %0, [%1]" : "=r" (value) : "r" (addr));
		return (T)value;
	}

	Genode::uint32_t value;

	if constexpr (sizeof(T) == 4)
		asm volatile ("ldr %w0, [%1]"  : "=r" (value) : "r" (addr));
	else if constexpr (sizeof(T) == 2)
		asm volatile ("ldrh %w0, [%1]" : "=r" (value) : "r" (addr));
	else if constexpr (sizeof(T) == 1)
		asm volatile ("ldrb %w0, [%1]" : "=r" (value) : "r" (addr));
	else
		static_assert(sizeof(T) == 4, "unsupported register width");

	return (T)value;
}


template <typename T>
inline void Hw::mmio_write(Genode::addr_t const addr, T const value)
{
	static_assert(T(-1) > T(0), "register type must be unsigned");

	if constexpr (sizeof(T) == 8) {
		Genode::uint64_t const v = value;
		asm volatile ("str %0, [%1]" : : "r" (v), "r" (addr) : "memory");
		return;
	}

	Genode::uint32_t const v = value;

	if constexpr (sizeof(T) == 4)
		asm volatile ("str %w0, [%1]"  : : "r" (v), "r" (addr) : "memory");
	else if constexpr (sizeof(T) == 2)
		asm volatile ("strh %w0, [%1]" : : "r" (v), "r" (addr) : "memory");
	else if constexpr (sizeof(T) == 1)
		asm volatile ("strb %w0, [%1]" : : "r" (v), "r" (addr) : "memory");
	else
		static_assert(sizeof(T) == 4, "unsupported register width");
}

#endif /* _INCLUDE__SPEC__ARM_64__CPU__MMIO_ACCESS_H_ */
