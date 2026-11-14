/*
 * \brief  Data types of the CPU frequency scaling (no Genode dependencies)
 * \author Ivan Loskutov
 * \date   2026-10-03
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _INCLUDE__CPUFREQ__TYPES_H_
#define _INCLUDE__CPUFREQ__TYPES_H_

namespace Cpufreq {

	enum { MAX_CLUSTERS = 3, MAX_STEPS = 32, MAX_CPUS = 8 };

	struct Steps;
	struct Cluster;
}


/**
 * Rates a cluster can be set to, in kHz, ascending
 */
struct Cpufreq::Steps
{
	unsigned count = 0;
	unsigned khz[MAX_STEPS] { };

	/**
	 * Add a rate, keeping the order; false if the table is full or the
	 * rate is 0 or already present
	 */
	bool add(unsigned rate)
	{
		if (!rate || count >= MAX_STEPS)
			return false;

		unsigned pos = 0;
		while (pos < count && khz[pos] < rate) pos++;

		if (pos < count && khz[pos] == rate)
			return false;

		for (unsigned i = count; i > pos; i--)
			khz[i] = khz[i - 1];

		khz[pos] = rate;
		count++;
		return true;
	}

	bool valid() const { return count > 0; }

	unsigned lowest()  const { return count ? khz[0]         : 0; }
	unsigned highest() const { return count ? khz[count - 1] : 0; }

	/* lowest step that is >= rate, the highest step if there is none */
	unsigned at_least(unsigned rate) const
	{
		for (unsigned i = 0; i < count; i++)
			if (khz[i] >= rate)
				return khz[i];
		return highest();
	}

	/* highest step that is <= rate, the lowest step if there is none */
	unsigned at_most(unsigned rate) const
	{
		for (unsigned i = count; i > 0; i--)
			if (khz[i - 1] <= rate)
				return khz[i - 1];
		return lowest();
	}
};


namespace Cpufreq {

	/**
	 * Take the steps of a rate table as the firmware provides it
	 *
	 * The table is accepted only if its rates are strictly monotonic (in
	 * either direction) and at least one of them lies within the limits;
	 * rates outside the limits are left out. On failure 'steps' keeps its
	 * content.
	 */
	inline bool steps_from_table(unsigned const *rates, unsigned n,
	                             unsigned min_khz, unsigned max_khz,
	                             Steps &steps)
	{
		if (n == 0 || n > MAX_STEPS)
			return false;

		bool ascending = true, descending = true;
		for (unsigned i = 1; i < n; i++) {
			if (rates[i] <= rates[i - 1]) ascending  = false;
			if (rates[i] >= rates[i - 1]) descending = false;
		}

		if (n > 1 && !ascending && !descending)
			return false;

		Steps result;
		for (unsigned i = 0; i < n; i++)
			if (rates[i] >= min_khz && rates[i] <= max_khz)
				if (!result.add(rates[i]))
					return false;

		if (!result.valid())
			return false;

		steps = result;
		return true;
	}
}


struct Cpufreq::Cluster
{
	unsigned id      = 0;   /* DVFS domain id (2, 3, 4) */
	unsigned khz     = 0;   /* current rate */
	unsigned min_khz = 0;   /* hardware limits */
	unsigned max_khz = 0;
	Steps    steps { };

	bool usable() const { return steps.valid() && max_khz && khz; }
};

#endif /* _INCLUDE__CPUFREQ__TYPES_H_ */
