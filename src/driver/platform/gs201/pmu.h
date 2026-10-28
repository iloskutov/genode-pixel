/*
 * \brief  Power domains of the GS201 power-management unit
 * \author Ivan Loskutov
 * \date   2026-02-08
 *
 * The register offsets of the domains are taken from the vendor's device
 * tree for the GS201 (nodes pd-disp, pd-dpu, and pd-hsi0).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SRC__DRIVER__PLATFORM__GS201__PMU_H_
#define _SRC__DRIVER__PLATFORM__GS201__PMU_H_

#include <os/attached_mmio.h>
#include <power.h>

namespace Driver { struct Pmu; }


struct Driver::Pmu : private Noncopyable
{
	Genode::Env &_env;
	Powers      &_powers;

	/*
	 * Power domain of the PMU
	 *
	 * The CONFIGURATION register (offset 0) switches the domain on by its
	 * bit 0, the STATUS register (offset 4) reflects the state in its bit 0.
	 *
	 * Powering up a domain from off additionally requires a call of the
	 * secure monitor to restore the domain's TZPC settings, which is not
	 * implemented. The boot loader leaves the display domains on, and
	 * switching them off would blank the panel. Hence, '_off' leaves a
	 * domain as it is.
	 */
	struct Power_domain : Power, private Genode::Mmio<0x8>
	{
		struct Config : Register<0x0, 32> { struct Enable : Bitfield<0, 1> { }; };
		struct Status : Register<0x4, 32> { struct Active : Bitfield<0, 1> { }; };

		/*
		 * Number of times the status is polled after a domain got switched
		 * on. It is a bound for the busy waiting, not a time.
		 */
		enum { MAX_STATUS_POLLS = 10000 };

		Power *_parent;

		Power_domain(Power_domain const &) = delete;
		Power_domain &operator = (Power_domain const &) = delete;

		Power_domain(Powers &powers, Name const &name,
		             Byte_range_ptr const &range, Power *parent)
		:
			Power(powers, name), Mmio(range), _parent(parent)
		{ }

		void _on() override
		{
			if (_parent) _parent->on();

			if (read<Status::Active>())
				return;

			Genode::warning("power domain ", name, " is off, powering it up "
			                "without the TZPC restore of the secure monitor");

			write<Config::Enable>(1);

			for (unsigned i = 0; i < MAX_STATUS_POLLS; i++)
				if (read<Status::Active>())
					return;

			Genode::warning("power domain ", name, " did not become active");
		}

		void _off() override { }
	};

	enum : addr_t {
		/* register block of the power-management unit */
		PMU_BASE = 0x18060000,
		PMU_SIZE = 0x4000,

		/* offsets of the power domains within the register block */
		PD_HSI0_OFFSET = 0x2080,
		PD_DPU_OFFSET  = 0x2200,
		PD_DISP_OFFSET = 0x2280,
	};

	Attached_mmio<0> _pmu { _env, {(char *)PMU_BASE, PMU_SIZE} };

	/* display: pd_disp, and pd_dpu with pd_disp as parent */
	Power_domain _disp_power { _powers, "disp-power",
	                           _pmu.range_at(PD_DISP_OFFSET), nullptr };
	Power_domain _dpu_power  { _powers, "dpu-power",
	                           _pmu.range_at(PD_DPU_OFFSET), &_disp_power };

	/*
	 * USB controller and PHYs: pd_hsi0, left on by the boot loader. On
	 * the phone, switching it off ended in an SError, so it stays on, as
	 * all domains here.
	 */
	Power_domain _usb_power  { _powers, "usb-power",
	                           _pmu.range_at(PD_HSI0_OFFSET), nullptr };

	Pmu(Genode::Env &env, Powers &powers) : _env(env), _powers(powers) { }
};

#endif /* _SRC__DRIVER__PLATFORM__GS201__PMU_H_ */
