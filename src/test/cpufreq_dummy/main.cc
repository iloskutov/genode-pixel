/*
 * \brief  Stand-in for a CPU frequency driver (test)
 * \author Ivan Loskutov
 * \date   2026-10-03
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>

/* local includes */
#include <cpufreq/request_policy.h>

namespace Cpufreq_dummy {

	using namespace Genode;

	struct Main;
}


struct Cpufreq_dummy::Main
{
	Env &_env;

	Attached_rom_dataspace _request { _env, "cpufreq_request" };

	Expanding_reporter _reporter { _env, "cpufreq", "cpufreq" };

	Cpufreq::Cluster _clusters[Cpufreq::MAX_CLUSTERS] { };

	/*
	 * Thermal model of cluster 4: it heats up by 3 C per 250 ms above
	 * 2048 MHz and cools down by 3 C otherwise, not below 60 C. The other
	 * clusters stay at 50 C.
	 */
	unsigned _temp_c = 60;

	Timer::Connection _timer { _env };

	void _handle_timer()
	{
		unsigned const old = _temp_c;

		if (_clusters[2].khz > 2048000) _temp_c += 3;
		else if (_temp_c >= 63)         _temp_c -= 3;

		if (_temp_c != old) {
			log("cluster 4: ", _temp_c, " C at ", _clusters[2].khz, " kHz");
			_report();
		}
	}

	Signal_handler<Main> _timer_handler {
		_env.ep(), *this, &Main::_handle_timer };

	static unsigned _cluster_of_cpu(unsigned cpu)
	{
		return cpu < 2 ? 0 : cpu == 2 ? 1 : 2;
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
					g.attribute("temp_c",  c.id == 4 ? _temp_c : 50U);
					for (unsigned i = 0; i < c.steps.count; i++)
						g.node("step", [&] {
							g.attribute("khz", c.steps.khz[i]); });
				});

			for (unsigned cpu = 0; cpu < 4; cpu++)
				g.node("cpu", [&] {
					Cpufreq::Cluster const &c = _clusters[_cluster_of_cpu(cpu)];
					g.attribute("xpos",    cpu);
					g.attribute("khz",     c.khz);
					g.attribute("cluster", c.id);
					g.attribute("temp_c",  c.id == 4 ? _temp_c : 50U);
				});
		});
	}

	void _handle_request()
	{
		_request.update();

		Cpufreq::Request requests[Cpufreq::MAX_CLUSTERS] { };

		_request.node().for_each_sub_node("cluster", [&] (Node const &node) {
			unsigned const id = node.attribute_value("id", ~0U);
			for (unsigned i = 0; i < Cpufreq::MAX_CLUSTERS; i++)
				if (_clusters[i].id == id)
					requests[i] = { true, node.attribute_value("khz", 0U) };
		});

		Cpufreq::Plan const plan = Cpufreq::plan_requests(_clusters, requests);

		struct Backend
		{
			Cpufreq::Cluster *clusters;

			unsigned set(unsigned index, unsigned khz)
			{
				log("cluster ", clusters[index].id, ": ", clusters[index].khz,
				    " -> ", khz, " kHz");
				return khz;
			}
		} backend { _clusters };

		Cpufreq::apply_plan(plan, _clusters, backend);

		if (plan.count)
			_report();
	}

	Signal_handler<Main> _request_handler {
		_env.ep(), *this, &Main::_handle_request };

	void _init(unsigned index, unsigned id, unsigned khz, unsigned min,
	           unsigned max, unsigned const *steps, unsigned num_steps)
	{
		Cpufreq::Cluster &c = _clusters[index];
		c.id = id; c.khz = khz; c.min_khz = min; c.max_khz = max;
		for (unsigned i = 0; i < num_steps; i++)
			c.steps.add(steps[i]);
	}

	Main(Env &env) : _env(env)
	{
		static unsigned const cl0[] {
			300000, 574000, 738000, 930000, 1098000, 1197000, 1328000,
			1401000, 1598000, 1803000 };
		static unsigned const cl1[] {
			400000, 799000, 1197000, 1491000, 1999000, 2348000 };
		static unsigned const cl2[] {
			500000, 851000, 1106000, 1426000, 1826000, 2048000, 2850000 };

		_init(0, 2, 1197000, 300000, 1803000, cl0, sizeof(cl0)/sizeof(cl0[0]));
		_init(1, 3, 1197000, 400000, 2348000, cl1, sizeof(cl1)/sizeof(cl1[0]));
		_init(2, 4, 1106000, 500000, 2850000, cl2, sizeof(cl2)/sizeof(cl2[0]));

		_report();

		_request.sigh(_request_handler);
		_handle_request();

		_timer.sigh(_timer_handler);
		_timer.trigger_periodic(250*1000);
	}
};


void Component::construct(Genode::Env &env)
{
	static Cpufreq_dummy::Main main(env);
}
