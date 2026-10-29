/*
 * \brief  Firmware images taken from ROM modules
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Back end of rom_firmware_lx.c: a firmware image requested by its Linux name
 * is the ROM module of the same name, used in place (the ROM dataspace is
 * attached, nothing is copied). A missing ROM module makes the request fail.
 *
 * A ROM dataspace is page-granular: its size is the image size rounded up
 * to the page size. A driver that depends on the exact image size declares
 * it with 'lx_emul_rom_firmware_size' before the image is requested (e.g.
 * from its configuration).
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/env.h>
#include <base/id_space.h>
#include <base/log.h>
#include <parent/parent.h>
#include <util/reconstructible.h>
#include <util/string.h>

/* lx_kit includes */
#include <lx_kit/env.h>

using namespace Genode;

namespace {

	struct Image
	{
		using Name = String<64>;

		Name          name      { };
		unsigned long size      { 0 };  /* declared size, 0 = unknown */
		unsigned      users     { 0 };

		Constructible<Attached_rom_dataspace> rom { };

		bool used() const { return name.valid(); }
	};

	enum { MAX_IMAGES = 16 };

	Image &image(unsigned i)
	{
		static Image images[MAX_IMAGES];
		return images[i];
	}

	/**
	 * Return true if a ROM session for the module can be had
	 *
	 * A ROM connection to a module that the parent denies does not fail:
	 * the component is stopped ("stop because parent denied ROM-session").
	 * For a firmware image that is fatal for no reason -- Linux drivers ask
	 * for images that may be missing and go on without them. The session is
	 * therefore tried by hand first, and closed again; the regular
	 * connection is opened only if that worked. (The amplifier driver,
	 * e.g., asks for its DSP firmware, which a scenario may leave out.)
	 */
	bool rom_available(Env &env, char const *name)
	{
		Parent::Client client { };
		Id_space<Parent::Client>::Element const id { client, env.id_space() };

		String<160> const args("ram_quota=10K, cap_quota=3, label=\"", name, "\"");

		bool available = false;

		env.try_session("ROM", id.id(), args.string(), Affinity()).with_result(
			[&] (Session_capability) { available = true; },
			[&] (Session_error)      { });

		if (available)
			env.close(id.id());

		return available;
	}

	Image *lookup(char const *name, bool create)
	{
		for (unsigned i = 0; i < MAX_IMAGES; i++)
			if (image(i).used() && image(i).name == name)
				return &image(i);

		if (!create)
			return nullptr;

		for (unsigned i = 0; i < MAX_IMAGES; i++)
			if (!image(i).used()) {
				image(i).name = Image::Name(name);
				return &image(i);
			}

		return nullptr;
	}
}


extern "C" void lx_emul_rom_firmware_size(char const *name, unsigned long size)
{
	Image * const img = lookup(name, true);

	if (!img) {
		error("rom_firmware: too many firmware images, cannot declare ", name);
		return;
	}
	img->size = size;
}


extern "C" int lx_emul_rom_firmware_open(char const *name, void const **data,
                                         unsigned long *size)
{
	if (!name || !data || !size)
		return -1;

	Image * const img = lookup(name, true);
	if (!img) {
		error("rom_firmware: too many firmware images, cannot open ", name);
		return -1;
	}

	if (!img->rom.constructed()) {

		/* a module nobody provides is a failed request, not the end */
		if (!rom_available(Lx_kit::env().env, name)) {
			/* free the slot, unless a size was declared for the name */
			if (!img->size)
				img->name = Image::Name();
			return -1;
		}

		try {
			img->rom.construct(Lx_kit::env().env, name);
		} catch (...) { }

		if (!img->rom.constructed() || !img->rom->valid()) {
			img->rom.destruct();
			return -1;
		}
	}

	unsigned long const ds_size = img->rom->size();

	if (img->size > ds_size) {
		error("rom_firmware: ", name, ": declared size ", img->size,
		      " exceeds the ROM module (", ds_size, " bytes)");
		if (!img->users)
			img->rom.destruct();
		return -1;
	}

	if (!img->size)
		warning("rom_firmware: ", name, ": size not declared, using the "
		        "page-rounded ROM size ", ds_size);

	img->users++;

	*data = img->rom->local_addr<void const>();
	*size = img->size ? img->size : ds_size;
	return 0;
}


extern "C" void lx_emul_rom_firmware_close(void const *data)
{
	for (unsigned i = 0; i < MAX_IMAGES; i++) {
		Image &img = image(i);

		if (!img.rom.constructed() || img.rom->local_addr<void const>() != data)
			continue;

		if (img.users)
			img.users--;

		if (!img.users)
			img.rom.destruct();
		return;
	}
	error("rom_firmware: close of an unknown image at ", data);
}
