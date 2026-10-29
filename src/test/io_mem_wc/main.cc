/*
 * \brief  Test of write-combined I/O memory: unaligned accesses
 * \author Ivan Loskutov
 * \date   2026-10-29
 *
 * Maps one page of I/O memory write-combined and reads it with unaligned
 * 16-, 32-, and 64-bit loads, each compared with the same bytes read one by
 * one. Write-combined I/O memory must be mapped as normal non-cacheable
 * memory, which allows unaligned accesses. Mapped as device memory, the
 * first unaligned load is an alignment fault and the test stops there.
 *
 * The page is the start of the AoC's reserved memory (aoc@94000000): no
 * RAM, so core hands it out as I/O memory, and the AoC is not running at
 * boot. The test only reads.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_io_mem_dataspace.h>
#include <base/component.h>

namespace Test {

	using namespace Genode;

	struct Main;
}


struct Test::Main
{
	enum : addr_t { BASE = 0x94000000, SIZE = 0x1000 };

	Env &_env;

	Attached_io_mem_dataspace _io_mem { _env, BASE, SIZE, true };

	uint8_t const volatile *_bytes() {
		return _io_mem.local_addr<uint8_t const volatile>(); }

	/* little-endian value of 'n' bytes at 'offset', read one by one */
	uint64_t _bytewise(unsigned offset, unsigned n)
	{
		uint64_t v = 0;
		for (unsigned i = 0; i < n; i++)
			v |= uint64_t(_bytes()[offset + i]) << (8*i);
		return v;
	}

	template <typename T>
	bool _unaligned(unsigned offset)
	{
		log("unaligned ", 8*sizeof(T), "-bit load at offset ", offset);

		T const v = *(T const volatile *)(_bytes() + offset);

		if (uint64_t(v) == _bytewise(offset, sizeof(T)))
			return true;

		error("FAIL ", 8*sizeof(T), "-bit load at offset ", offset, ": ",
		      Hex(uint64_t(v)), ", bytes ", Hex(_bytewise(offset, sizeof(T))));
		return false;
	}

	Main(Env &env) : _env(env)
	{
		log("I/O memory ", Hex(BASE), " mapped write-combined");

		if (_unaligned<uint16_t>(1)
		 && _unaligned<uint32_t>(1)
		 && _unaligned<uint32_t>(6)
		 && _unaligned<uint64_t>(3)
		 && _unaligned<uint64_t>(SIZE - 9))
			log("PASS");
	}
};


void Component::construct(Genode::Env &env) { static Test::Main main(env); }
