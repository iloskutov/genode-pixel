/*
 * \brief  From rate requests to an ordered list of rate changes
 * \author Ivan Loskutov
 * \date   2026-10-03
 *
 * Limits, steps and the constraints between the clusters, as the reference
 * kernel applies them (exynos-acme limits, exynos-dm constraint tables of
 * gs201-cpu.dtsi). No Genode dependencies.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__CPUFREQ__REQUEST_POLICY_H_
#define _INCLUDE__CPUFREQ__REQUEST_POLICY_H_

#include <cpufreq/types.h>

namespace Cpufreq {

	struct Request { bool present = false; unsigned khz = 0; };
	struct Change  { unsigned index = 0;   unsigned khz = 0; };

	struct Plan
	{
		unsigned count = 0;
		Change   change[MAX_CLUSTERS] { };

		void add(unsigned index, unsigned khz) { change[count++] = { index, khz }; }
	};

	struct Constraint_row { unsigned khz; unsigned cl0_khz; };

	/* dm-table med_ank_perf: rate of CL1 -> minimum rate of CL0, ascending */
	static constexpr Constraint_row cl1_constraints[] = {
		{  400000,  300000 }, {  553000,  574000 }, {  696000,  574000 },
		{  799000,  574000 }, {  910000,  574000 }, { 1024000,  738000 },
		{ 1197000,  738000 }, { 1328000,  930000 }, { 1491000,  930000 },
		{ 1663000, 1098000 }, { 1836000, 1098000 }, { 1999000, 1098000 },
		{ 2130000, 1098000 }, { 2253000, 1197000 }, { 2348000, 1328000 } };

	/* dm-table big_ank_perf: rate of CL2 -> minimum rate of CL0, ascending */
	static constexpr Constraint_row cl2_constraints[] = {
		{  500000,  300000 }, {  851000,  574000 }, {  984000,  738000 },
		{ 1106000,  738000 }, { 1277000,  738000 }, { 1426000,  930000 },
		{ 1582000,  930000 }, { 1745000, 1098000 }, { 1826000, 1098000 },
		{ 2048000, 1197000 }, { 2188000, 1328000 }, { 2252000, 1328000 },
		{ 2401000, 1401000 }, { 2507000, 1401000 }, { 2630000, 1401000 },
		{ 2704000, 1401000 }, { 2802000, 1401000 }, { 2850000, 1401000 } };

	/**
	 * Minimum rate of CL0 that a rate of cluster 'index' (1 or 2) requires
	 *
	 * A rate between two rows takes the row above it, a rate above the
	 * table the last row.
	 */
	inline unsigned required_cl0_khz(unsigned index, unsigned khz)
	{
		auto lookup = [&] (Constraint_row const *rows, unsigned n) {
			for (unsigned i = 0; i < n; i++)
				if (rows[i].khz >= khz)
					return rows[i].cl0_khz;
			return rows[n - 1].cl0_khz;
		};

		if (index == 1)
			return lookup(cl1_constraints,
			              sizeof(cl1_constraints)/sizeof(cl1_constraints[0]));
		if (index == 2)
			return lookup(cl2_constraints,
			              sizeof(cl2_constraints)/sizeof(cl2_constraints[0]));
		return 0;
	}

	/**
	 * Rate changes for a set of requests, in the order to apply them
	 *
	 * Index 0 is CL0, 1 is CL1, 2 is CL2. A cluster without a request keeps
	 * its rate, a cluster without a step table is never changed.
	 */
	inline Plan plan_requests(Cluster const clusters[MAX_CLUSTERS],
	                          Request const requests[MAX_CLUSTERS])
	{
		unsigned target[MAX_CLUSTERS];

		for (unsigned i = 0; i < MAX_CLUSTERS; i++) {
			Cluster const &c = clusters[i];
			target[i] = c.khz;

			if (!requests[i].present || !c.steps.valid())
				continue;

			unsigned khz = requests[i].khz;
			if (khz < c.min_khz) khz = c.min_khz;
			if (khz > c.max_khz) khz = c.max_khz;

			target[i] = c.steps.at_least(khz);
		}

		Cluster const &cl0 = clusters[0];

		if (cl0.steps.valid()) {

			/* CL0 follows what the other clusters require */
			for (unsigned i = 1; i < MAX_CLUSTERS; i++) {
				unsigned const need = required_cl0_khz(i, target[i]);
				if (need > target[0])
					target[0] = cl0.steps.at_least(need);
			}
		} else {

			/* CL0 cannot be changed: the others stay within what it allows */
			for (unsigned i = 1; i < MAX_CLUSTERS; i++) {
				Steps const &steps = clusters[i].steps;
				if (!steps.valid())
					continue;

				while (required_cl0_khz(i, target[i]) > cl0.khz
				    && target[i] > steps.lowest())
					target[i] = steps.at_most(target[i] - 1);
			}
		}

		Plan plan;

		if (target[0] > cl0.khz)
			plan.add(0, target[0]);

		for (unsigned i = 1; i < MAX_CLUSTERS; i++)
			if (target[i] != clusters[i].khz)
				plan.add(i, target[i]);

		if (target[0] < cl0.khz)
			plan.add(0, target[0]);

		return plan;
	}

	/**
	 * Apply a plan through 'backend.set(index, khz)', which returns the rate
	 * the cluster has afterwards, or 0 if the request could not be sent
	 *
	 * The rate reported by the backend is recorded in the cluster. The first
	 * change that does not arrive at its rate ends the plan: the changes
	 * after it rely on it (CL0 is raised before the cluster that needs it).
	 *
	 * \return true if every change took effect
	 */
	inline bool apply_plan(Plan const &plan, Cluster clusters[MAX_CLUSTERS],
	                       auto &backend)
	{
		for (unsigned i = 0; i < plan.count; i++) {
			Change const &change = plan.change[i];

			unsigned const rate = backend.set(change.index, change.khz);

			if (rate)
				clusters[change.index].khz = rate;

			if (rate != change.khz)
				return false;
		}
		return true;
	}
}

#endif /* _INCLUDE__CPUFREQ__REQUEST_POLICY_H_ */
