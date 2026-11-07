/*
 * \brief  Test of the modem driver: boot loader of the CP over PCIe
 * \author Ivan Loskutov
 * \date   2026-11-07
 *
 * Takes the first steps of the vendor's CP boot daemon (cbd) through the
 * device node "umts_boot0" of pixel_modem:
 *
 * - IOCTL_POWER_ON,
 * - IOCTL_LOAD_CP_IMAGE with the section "BOOT" of modem.bin, which the
 *   driver places in the memory shared with the CP,
 * - IOCTL_START_CP_BOOTLOADER, during which the CP's boot ROM fetches the
 *   boot loader over the PCIe link (boot stage 0x3fff), the boot loader
 *   signals the AP and the driver brings the link up again for the
 *   firmware.
 *
 * The firmware itself (MAIN, VSS, NV) is not sent, the CP stays in its
 * boot loader. modem.bin is the one of the firmware partition modem_a,
 * mounted at the root of the VFS: '/images/<version>/modem.bin', the
 * first version directory that holds one is taken.
 *
 * The test ends with "modem_boot: PASS" or "modem_boot: FAIL".
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <os/vfs.h>
#include <util/reconstructible.h>

#include <android_chardev_session/connection.h>

namespace Test {

	using namespace Genode;

	struct Toc_entry;
	struct Cp_image;
	struct Main;

	using Chardev = Android_chardev_session::Session;
}


/* one entry of the table of contents at the start of modem.bin */
struct Test::Toc_entry
{
	char     name[12];
	uint32_t img_offset;   /* offset of the section in modem.bin */
	uint32_t mem_offset;   /* load address on the CP */
	uint32_t size;
	uint32_t crc;
	uint32_t reserved;
} __attribute__((packed));


/* argument of IOCTL_LOAD_CP_IMAGE (cpif modem_prj.h), the image follows it */
struct Test::Cp_image
{
	uint64_t binary;
	uint32_t size, m_offset, b_offset, mode, len;
} __attribute__((packed));


struct Test::Main
{
	/* Linux ioctl numbers of the cpif (modem_prj.h, magic 'o') */
	enum : uint64_t {
		IOCTL_POWER_ON            = 0x6f19,
		IOCTL_START_CP_BOOTLOADER = 0x40046f22,   /* struct boot_mode */
		IOCTL_LOAD_CP_IMAGE       = 0x401c6f40,   /* struct cp_image */
	};

	enum { TOC_ENTRIES = 16, CP_BOOT_MODE_NORMAL = 0 };

	static_assert(sizeof(Toc_entry) == 32 && sizeof(Cp_image) == 28);

	using Path = Directory::Path;

	Env &_env;

	Heap _heap { _env.ram(), _env.rm() };

	Attached_rom_dataspace _config { _env, "config" };

	Root_directory _root { _env, _heap };

	void _finish(bool pass)
	{
		log("modem_boot: ", pass ? "PASS" : "FAIL");
		_env.parent().exit(pass ? 0 : 1);
	}

	Path _find_modem_bin()
	{
		Path result { };

		Directory images { _root, "images" };
		images.for_each_entry([&] (Directory::Entry const &e) {
			Path const path { "/images/", e.name(), "/modem.bin" };
			if (!result.valid() && e.dir() && _root.file_exists(path))
				result = path;
		});
		return result;
	}

	bool _boot_section(Readonly_file const &file, Toc_entry &boot)
	{
		Toc_entry toc[TOC_ENTRIES] { };

		Byte_range_ptr const range { (char *)toc, sizeof(toc) };
		if (file.read(Readonly_file::At { 0 }, range) != sizeof(toc)) {
			error("cannot read the table of contents");
			return false;
		}

		for (Toc_entry const &e : toc) {
			if (!e.name[0])
				break;

			String<13> const name { Cstring(e.name, sizeof(e.name)) };
			uint32_t   const offset = e.img_offset, size = e.size;
			log("section ", name, ": offset ", Hex(offset), " size ", Hex(size));

			if (name == "BOOT") {
				boot = e;
				return true;
			}
		}
		error("no section BOOT in modem.bin");
		return false;
	}

	bool _ioctl(Android_chardev_session::Connection &dev, char const *name,
	            uint64_t cmd, uint32_t in_len)
	{
		log(name, " ...");
		Chardev::Ioctl_result const r = dev.ioctl(cmd, 0, in_len, 0);

		bool const ok = r.result == Chardev::OK && r.ret == 0;
		if (ok)
			log(name, " -> 0");
		else
			error(name, " failed, errno ", r.err, ", return value ", r.ret);
		return ok;
	}

	bool _run()
	{
		Path const path = _find_modem_bin();
		if (!path.valid()) {
			error("no /images/<version>/modem.bin");
			return false;
		}
		log("firmware ", path);

		Readonly_file const file { _root, path };

		Toc_entry boot_entry { };
		if (!_boot_section(file, boot_entry))
			return false;

		uint32_t const boot_offset = boot_entry.img_offset,
		               boot_size   = boot_entry.size;

		Android_chardev_session::Connection dev { _env, "umts_boot0" };
		Attached_dataspace buffer { _env.rm(), dev.dataspace() };

		if (sizeof(Cp_image) + boot_size > buffer.size()) {
			error("BOOT of ", boot_size, " bytes exceeds the session buffer");
			return false;
		}

		if (!_ioctl(dev, "POWER_ON", IOCTL_POWER_ON, 0))
			return false;

		/* the driver takes the image from behind the struct */
		Cp_image &img = *buffer.local_addr<Cp_image>();
		img = { .binary = 0, .size = boot_size, .m_offset = 0, .b_offset = 0,
		        .mode = 0, .len = boot_size };

		Byte_range_ptr const image { buffer.local_addr<char>() + sizeof(Cp_image),
		                             boot_size };
		if (file.read(Readonly_file::At { boot_offset }, image) != boot_size) {
			error("cannot read BOOT");
			return false;
		}

		if (!_ioctl(dev, "LOAD_CP_IMAGE", IOCTL_LOAD_CP_IMAGE,
		            uint32_t(sizeof(Cp_image) + boot_size)))
			return false;

		*buffer.local_addr<uint32_t>() = CP_BOOT_MODE_NORMAL;
		return _ioctl(dev, "START_CP_BOOTLOADER", IOCTL_START_CP_BOOTLOADER, 4);
	}

	Main(Env &env) : _env(env)
	{
		_config.node().with_optional_sub_node("vfs", [&] (Node const &vfs) {
			_root.apply_config(vfs); });

		_finish(_run());
	}
};


void Component::construct(Genode::Env &env)
{
	static Test::Main main(env);
}
