/*
 * \brief  Firmware images of the panthor GPU driver, obtained as ROM
 * \author Ivan Loskutov
 * \date   2026-10-07
 *
 * The driver asks for "arm/mali/arch<major>.<minor>/mali_csffw.bin". The
 * ROM requested from the parent carries the last path element only
 * ("mali_csffw.bin"); the image is no part of the repository.
 *
 * The size handed to the driver is the size of the dataspace, i.e., the
 * image size rounded up to the page size. The driver takes the real size
 * from the image header and only checks that it does not exceed this value.
 */

/*
 * Copyright (C) 2026 Ivan Loskutov
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <util/reconstructible.h>

/* lx emul/kit includes */
#include <lx_kit/env.h>

/* local includes */
#include "emul.h"

using namespace Genode;


static Constructible<Attached_rom_dataspace> &_firmware_rom()
{
	static Constructible<Attached_rom_dataspace> rom { };
	return rom;
}


static char const *_last_element(char const *path)
{
	char const *name = path;
	for (char const *p = path; *p; p++)
		if (*p == '/')
			name = p + 1;
	return name;
}


int emul_firmware_request(char const *path, void const **data,
                          unsigned long *size)
{
	if (!path || !data || !size)
		return -1;

	char const * const name = _last_element(path);

	if (_firmware_rom().constructed()) {
		error("firmware '", path, "' requested while another one is in use");
		return -1;
	}

	try {
		_firmware_rom().construct(Lx_kit::env().env, name);
	} catch (...) {
		error("firmware ROM '", name, "' (", path, ") unavailable");
		return -1;
	}

	*data = _firmware_rom()->local_addr<void const>();
	*size = _firmware_rom()->size();

	log("firmware '", path, "' from ROM '", name, "', ", *size, " bytes");
	return 0;
}


void emul_firmware_release(void const *data)
{
	if (!_firmware_rom().constructed())
		return;

	if (_firmware_rom()->local_addr<void const>() != data) {
		error("release of unknown firmware image");
		return;
	}

	_firmware_rom().destruct();
}
