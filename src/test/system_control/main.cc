/*
 * \brief  Test of the system-control interface of core (SMC forwarding)
 * \author Ivan Loskutov
 * \date   2026-10-29
 *
 * Issues read-only SMCs through the PD session's system-control capability:
 *
 * - the SMCCC "Call UID" query of the standard service, whose result is
 *   Arm's UUID 108d905b-f863-47e8-ae2d-c0fb5641f6e2 in w0..w3, so that all
 *   four result registers have to reach the component,
 * - Trusty's version string: SMC_FC_GET_VERSION_STR with the index -1 in
 *   x1 returns the length, with an index the character at it.
 *
 * The component needs 'managing_system: yes'.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/component.h>
#include <base/rpc_client.h>
#include <pd_session/pd_session.h>

namespace Test {

	using namespace Genode;

	struct System_control;
	struct Main;
}


struct Test::System_control : Rpc_client<Pd_session::System_control>
{
	using State = Pd_session::Managing_system_state;

	explicit System_control(Capability<Pd_session::System_control> cap)
	: Rpc_client<Pd_session::System_control>(cap) { }

	State system_control(State const &state) override {
		return call<Rpc_system_control>(state); }

	/* result of a fast call with one argument */
	State smc_state(uint32_t func, uint32_t arg)
	{
		State in { };
		in.r[0] = func;
		in.r[1] = arg;
		return system_control(in);
	}

	/* w0 of the result of a fast call with one argument */
	int smc(uint32_t func, uint32_t arg) {
		return (int)(uint32_t)smc_state(func, arg).r[0]; }
};


struct Test::Main
{
	enum : uint32_t {
		STANDARD_CALL_UID      = 0x8400ff01,
		TRUSTY_GET_VERSION_STR = 0xbc00000a,   /* SMC_FC_GET_VERSION_STR */
	};

	/* 108d905b-f863-47e8-ae2d-c0fb5641f6e2 as w0..w3 (SMCCC) */
	static constexpr uint32_t STANDARD_UID[4] {
		0x108d905b, 0x47e8f863, 0xfbc02dae, 0xe2f64156 };

	enum { MAX_LEN = 160 };

	Env &_env;

	Main(Env &env) : _env(env)
	{
		Capability<Pd_session::System_control> const cap =
			_env.pd().system_control_cap(Affinity::Location());

		if (!cap.valid()) {
			error("FAIL no system-control capability (managing_system missing?)");
			return;
		}

		System_control sc { cap };

		System_control::State const uid = sc.smc_state(STANDARD_CALL_UID, 0);

		log("standard service UID: ", Hex((uint32_t)uid.r[0]), " ",
		    Hex((uint32_t)uid.r[1]), " ", Hex((uint32_t)uid.r[2]), " ",
		    Hex((uint32_t)uid.r[3]));

		for (unsigned i = 0; i < 4; i++) {
			if ((uint32_t)uid.r[i] != STANDARD_UID[i]) {
				error("FAIL w", i, " of the standard service UID: ",
				      Hex((uint32_t)uid.r[i]), ", expected ", Hex(STANDARD_UID[i]));
				return;
			}
		}

		int const len = sc.smc(TRUSTY_GET_VERSION_STR, ~0u);
		if (len <= 0 || len > MAX_LEN) {
			error("FAIL length of the version string: ", len);
			return;
		}

		char str[MAX_LEN + 1] { };
		for (int i = 0; i < len; i++) {
			int const c = sc.smc(TRUSTY_GET_VERSION_STR, i);
			if (c <= 0 || c > 0x7e) {
				error("FAIL character ", i, " of the version string: ", c);
				return;
			}
			str[i] = (char)c;
		}

		log("Trusty version: \"", Cstring(str), "\"");
		log("PASS");
	}
};


void Component::construct(Genode::Env &env) { static Test::Main main(env); }
