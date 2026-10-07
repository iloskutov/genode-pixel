/*
 * \brief   CPU state at the hand-off from the firmware of the Google Pixel 7
 * \author  Ivan Loskutov
 * \date    2026-10-07
 *
 * Each CPU enters bootstrap at EL2 with a state that the generic arm_64
 * code does not expect. It is brought in order here, before the generic
 * code runs.
 */

/*
 * SPDX-License-Identifier: MIT
 */

.section .text

	/******************************
	 ** Entry of secondary CPUs  **
	 ******************************/

	.global _pixel7_start_secondary
	_pixel7_start_secondary:
	bl pixel7_sanitize_cpu_state
	b  _crt0_start_secondary


	/******************************************************
	 ** void pixel7_sanitize_cpu_state()                 **
	 **                                                  **
	 ** Needs no stack, modifies x8 only                 **
	 ******************************************************/

	.global pixel7_sanitize_cpu_state
	pixel7_sanitize_cpu_state:

	/*
	 * The kernel enables the physical timer without programming it. Let it
	 * expire right away instead of at the compare value left by the
	 * firmware, which may lie far in the future.
	 */
	mov  x8, #1
	msr  cntp_tval_el0, x8

	mrs  x8, CurrentEL
	cmp  x8, #(2 << 2)
	b.ne 1f

	/*
	 * The firmware may hand over with the virtualization host extensions
	 * enabled (HCR_EL2.E2H), which changes the layout of other EL2
	 * registers and redirects accesses of EL1 registers. Keep only RW
	 * (EL1 is aarch64) before any other register is touched.
	 */
	mov  x8, #(1 << 31)
	msr  hcr_el2, x8
	isb

	/* no traps of debug and performance-monitor accesses to EL2 */
	msr  mdcr_el2, xzr

	/*
	 * WXN, E0E, and EE of SCTLR_EL1 are UNKNOWN after reset, the generic
	 * code keeps them as found
	 */
	mrs  x8, sctlr_el1
	bic  x8, x8, #(1 << 19)
	bic  x8, x8, #(3 << 24)
	msr  sctlr_el1, x8
	isb
	1:
	ret
