/*
 * \brief  CPU frequency scaling through the ACPM firmware
 * \author Ivan Loskutov
 * \date   2026-10-03
 *
 * The three CPU clusters are DVFS domains of the ACPM firmware. Their steps
 * come from the firmware's frequency/voltage map, their rates are read and
 * set with IPC messages (reference: cal-if/fvmap.c, cal-if/acpm_dvfs.c).
 * Requests arrive as ROM "cpufreq_request", the state is published as
 * report "cpufreq". The report also carries the temperature of each
 * cluster's thermal zone (thermal/samsung/exynos_acpm_tmu.c), read
 * periodically.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _SRC__DRIVER__PLATFORM__GS201__CPUFREQ_H_
#define _SRC__DRIVER__PLATFORM__GS201__CPUFREQ_H_

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <os/reporter.h>
#include <timer_session/connection.h>

/* local includes */
#include <acpm.h>
#include <cpufreq/request_policy.h>

namespace Driver { struct Cpufreq_driver; }


struct Driver::Cpufreq_driver : private Noncopyable
{
	Genode::Env &_env;
	Acpm        &_acpm;

	enum {
		FVMAP_BASE        = 0x31000,  /* 'fvmap_addr' of gs201.dtsi */
		FVMAP_HEADER_SIZE = 24,
		FVMAP_NUM_OF_LV   = 1,        /* u8  */
		FVMAP_O_RATEVOLT  = 20,       /* u16 */
		NUM_CPUS          = 8,
	};

	Cpufreq::Cluster _clusters[Cpufreq::MAX_CLUSTERS] { };

	/* temperature of each cluster's thermal zone, as last read */
	struct Temperature { bool known = false; unsigned celsius = 0; };

	Temperature _temperature[Cpufreq::MAX_CLUSTERS] { };

	/* thermal zone of cluster index 0 (CL0), 1 (CL1), 2 (CL2) */
	static unsigned _zone_of_cluster(unsigned index)
	{
		return index == 0 ? Acpm::TMU_ZONE_LITTLE
		     : index == 1 ? Acpm::TMU_ZONE_MID : Acpm::TMU_ZONE_BIG;
	}

	Timer::Connection _thermal_timer { _env };

	Signal_handler<Cpufreq_driver> _thermal_handler {
		_env.ep(), *this, &Cpufreq_driver::_handle_thermal };

	Expanding_reporter _reporter { _env, "cpufreq", "cpufreq" };

	unsigned _max_attempts = 0;

	/*
	 * Cost of the rate changes, logged every 10 s with 'cpufreq_stats'
	 */
	struct Stats
	{
		bool     enabled   = false;
		unsigned changes   = 0;
		unsigned requests  = 0;   /* 'cpufreq_request' updates */
		unsigned reads     = 0;   /* rate reads of the firmware */
		uint64_t change_us = 0;   /* in 'set' */
		uint64_t last_ms   = 0;
	} _stats { };
	bool     _resync       = false;

	Attached_rom_dataspace _request { _env, "cpufreq_request" };

	Signal_handler<Cpufreq_driver> _request_handler {
		_env.ep(), *this, &Cpufreq_driver::_handle_request };

	static unsigned _cluster_of_cpu(unsigned cpu)
	{
		return cpu < 4 ? 0 : cpu < 6 ? 1 : 2;
	}

	/**
	 * Read the steps of a domain from the firmware's map
	 *
	 * The table is taken only if it is plausible: 1 to 32 levels, rates
	 * strictly monotonic, at least one within the domain's limits.
	 */
	void _read_steps(Cpufreq::Cluster &cluster)
	{
		unsigned const header = FVMAP_BASE + cluster.id*FVMAP_HEADER_SIZE;
		unsigned const levels = _acpm.sram_read8(header + FVMAP_NUM_OF_LV);
		unsigned const table  = FVMAP_BASE
		                      + _acpm.sram_read16(header + FVMAP_O_RATEVOLT);

		if (levels == 0 || levels > Cpufreq::MAX_STEPS) {
			error("cpufreq: domain ", cluster.id, " has ", levels,
			      " levels, left alone");
			return;
		}

		unsigned rates[Cpufreq::MAX_STEPS];
		for (unsigned i = 0; i < levels; i++)
			rates[i] = _acpm.sram_read32(table + i*8);

		if (!Cpufreq::steps_from_table(rates, levels, cluster.min_khz,
		                               cluster.max_khz, cluster.steps))
			error("cpufreq: domain ", cluster.id, " has an implausible table "
			      "of ", levels, " levels (first rate ", rates[0],
			      " kHz), left alone");
	}

	void _report()
	{
		_reporter.generate([&] (Generator &g) {

			for (Cpufreq::Cluster const &c : _clusters)
				g.node("cluster", [&] {
					g.attribute("id",      c.id);
					g.attribute("khz",     c.khz);
					g.attribute("min_khz", c.min_khz);
					g.attribute("max_khz", c.max_khz);
					Temperature const &t = _temperature[&c - _clusters];
					if (t.known)
						g.attribute("temp_c", t.celsius);
					for (unsigned i = 0; i < c.steps.count; i++)
						g.node("step", [&] {
							g.attribute("khz", c.steps.khz[i]); });
				});

			for (unsigned cpu = 0; cpu < NUM_CPUS; cpu++)
				g.node("cpu", [&] {
					Cpufreq::Cluster const &c = _clusters[_cluster_of_cpu(cpu)];
					g.attribute("xpos",    cpu);
					g.attribute("khz",     c.khz);
					g.attribute("cluster", c.id);
					Temperature const &t = _temperature[_cluster_of_cpu(cpu)];
					if (t.known)
						g.attribute("temp_c", t.celsius);
				});
		});
	}

	void _init(unsigned index, unsigned id, unsigned min, unsigned max)
	{
		Cpufreq::Cluster &c = _clusters[index];

		c.id      = id;
		c.min_khz = min;
		c.max_khz = max;
		c.khz     = _acpm.dvfs_rate(id);

		_read_steps(c);

		/*
		 * One line per domain, not one per step: the driver answers no
		 * session request while it starts, and 44 lines on the serial line
		 * took half a second (notes/big_cores.md, boot stall). The steps
		 * are in the 'cpufreq' report.
		 */
		log("cpufreq: domain ", id, " at ", c.khz, " kHz, ",
		    c.steps.count, " steps from ", c.steps.lowest(), " to ",
		    c.steps.highest(), " kHz");
	}

	/**
	 * Backend of 'Cpufreq::apply_plan': set the rate of one cluster and
	 * return what the firmware has afterwards (0: request not sent)
	 */
	unsigned set(unsigned index, unsigned khz)
	{
		Cpufreq::Cluster const &cluster = _clusters[index];

		if (!_acpm.dvfs_request(cluster.id, khz)) {
			warning("cpufreq: domain ", cluster.id, " request of ", khz,
			        " kHz not sent, queue full");
			return 0;
		}

		uint64_t const start_us = _acpm._timer.elapsed_us();

		/* the firmware changes voltage and PLL, give it time before the read */
		uint32_t rate     = 0;
		unsigned attempts = 0;
		while (attempts < 20) {
			attempts++;
			_acpm._timer.usleep(500);
			rate = _acpm.dvfs_rate(cluster.id);
			if (rate == khz)
				break;
		}

		_stats.changes++;
		_stats.reads     += attempts;
		_stats.change_us += _acpm._timer.elapsed_us() - start_us;

		/* how long the firmware takes at most, for the notes */
		if (attempts > _max_attempts) {
			_max_attempts = attempts;
			log("cpufreq: domain ", cluster.id, " took ", attempts,
			    " reads of 0.5 ms to reach ", khz, " kHz");
		}

		if (rate != khz)
			error("cpufreq: domain ", cluster.id, " asked for ", khz,
			      " kHz, firmware reports ", rate, " kHz");

		return rate;
	}

	void _handle_request()
	{
		_request.update();
		_stats.requests++;

		/*
		 * After a change that did not take effect the recorded rates may be
		 * behind the firmware: read them before planning on them.
		 */
		if (_resync) {
			for (Cpufreq::Cluster &cluster : _clusters)
				if (uint32_t const rate = _acpm.dvfs_rate(cluster.id))
					cluster.khz = rate;
			_resync = false;
		}

		Cpufreq::Request requests[Cpufreq::MAX_CLUSTERS] { };

		_request.node().for_each_sub_node("cluster", [&] (Node const &node) {
			unsigned const id = node.attribute_value("id", ~0U);
			for (unsigned i = 0; i < Cpufreq::MAX_CLUSTERS; i++)
				if (_clusters[i].id == id && node.has_attribute("khz"))
					requests[i] = { true, node.attribute_value("khz", 0U) };
		});

		Cpufreq::Plan const plan = Cpufreq::plan_requests(_clusters, requests);

		/* a failed change ends the plan, the rest waits for the next request */
		if (!Cpufreq::apply_plan(plan, _clusters, *this))
			_resync = true;

		if (plan.count)
			_report();
	}

	/**
	 * Read the clusters' temperatures
	 *
	 * \return true if a value changed
	 */
	bool _read_temperatures()
	{
		bool changed = false;

		for (unsigned i = 0; i < Cpufreq::MAX_CLUSTERS; i++) {

			Acpm::Tmu_reading const r = _acpm.tmu_temp(_zone_of_cluster(i));

			/* a failed read, an error code or 0 C is no temperature */
			Temperature const t { r.ok && r.ret >= 0 && r.temp_c > 0, r.temp_c };

			if (t.known != _temperature[i].known
			 || (t.known && t.celsius != _temperature[i].celsius))
				changed = true;

			if (!t.known && _temperature[i].known)
				warning("cpufreq: no temperature for domain ", _clusters[i].id,
				        " (ok ", r.ok, ", ret ", r.ret, ")");

			_temperature[i] = t;
		}
		return changed;
	}

	void _handle_thermal()
	{
		if (_read_temperatures())
			_report();

		uint64_t const now_ms = _thermal_timer.elapsed_ms();
		if (_stats.enabled && now_ms - _stats.last_ms >= 10000) {
			log("cpufreq: ", now_ms - _stats.last_ms, " ms: ",
			    _stats.requests, " requests, ", _stats.changes, " changes, ",
			    _stats.reads, " rate reads, ", _stats.change_us/1000,
			    " ms in changes");
			_stats = { .enabled = true, .changes = 0, .requests = 0,
			           .reads = 0, .change_us = 0, .last_ms = now_ms };
		}
	}

	Cpufreq_driver(Genode::Env &env, Acpm &acpm, uint64_t thermal_period_ms,
	               bool stats)
	:
		_env(env), _acpm(acpm)
	{
		_stats.enabled = stats;

		if (!_acpm.dvfs_available())
			error("cpufreq: ACPM firmware provides no DVFS channel");
		if (!_acpm.tmu_available())
			warning("cpufreq: ACPM firmware provides no thermal channel");

		/* limits of the exynos-acme domains in gs201-cpu.dtsi */
		_init(0, Acpm::DVFS_CPUCL0, 300000, 1803000);
		_init(1, Acpm::DVFS_CPUCL1, 400000, 2348000);
		_init(2, Acpm::DVFS_CPUCL2, 500000, 2850000);

		_read_temperatures();

		for (unsigned i = 0; i < Cpufreq::MAX_CLUSTERS; i++)
			if (!_temperature[i].known)
				warning("cpufreq: domain ", _clusters[i].id,
				        " has no temperature");

		_report();

		_request.sigh(_request_handler);
		_handle_request();

		_thermal_timer.sigh(_thermal_handler);
		if (thermal_period_ms)
			_thermal_timer.trigger_periodic(thermal_period_ms*1000);
	}
};

#endif /* _SRC__DRIVER__PLATFORM__GS201__CPUFREQ_H_ */
