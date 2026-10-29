/*
 * \brief  Linux firmware-loader interface on top of ROM modules
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * Replaces drivers/base/firmware_loader for drivers that list this file
 * (and rom_firmware.cc) in their target: 'request_firmware("x")' yields the
 * content of the ROM module "x". The image is not copied, 'fw->data' points
 * into the attached ROM dataspace and is read-only.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

extern int  lx_emul_rom_firmware_open(char const *name, void const **data,
                                      unsigned long *size);
extern void lx_emul_rom_firmware_close(void const *data);


static int rom_firmware_request(const struct firmware **firmware_p,
                                const char *name, struct device *device,
                                bool warn)
{
	struct firmware *fw;
	void const      *data = NULL;
	unsigned long    size = 0;

	if (!firmware_p || !name)
		return -EINVAL;

	*firmware_p = NULL;

	fw = kzalloc(sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return -ENOMEM;

	if (lx_emul_rom_firmware_open(name, &data, &size)) {
		if (warn)
			printk("rom_firmware: firmware \"%s\" not available (no ROM "
			       "module of that name)\n", name);
		kfree(fw);
		return -ENOENT;
	}

	fw->data = data;
	fw->size = size;

	*firmware_p = fw;
	return 0;
}


int request_firmware(const struct firmware **firmware_p, const char *name,
                     struct device *device)
{
	return rom_firmware_request(firmware_p, name, device, true);
}


int firmware_request_nowarn(const struct firmware **firmware_p,
                            const char *name, struct device *device)
{
	return rom_firmware_request(firmware_p, name, device, false);
}


void release_firmware(const struct firmware *fw)
{
	if (!fw)
		return;

	lx_emul_rom_firmware_close(fw->data);
	kfree(fw);
}


struct rom_firmware_work
{
	struct work_struct  work;
	const char         *name;
	struct device      *device;
	void               *context;
	void              (*cont)(const struct firmware *fw, void *context);
};


static void rom_firmware_work_func(struct work_struct *work)
{
	struct rom_firmware_work *fw_work =
		container_of(work, struct rom_firmware_work, work);

	const struct firmware *fw = NULL;

	/* as the Linux loader: the callback gets NULL if the image is missing */
	rom_firmware_request(&fw, fw_work->name, fw_work->device, true);

	fw_work->cont(fw, fw_work->context);

	kfree_const(fw_work->name);
	kfree(fw_work);
}


int request_firmware_nowait(struct module *module, bool uevent,
                            const char *name, struct device *device,
                            gfp_t gfp, void *context,
                            void (*cont)(const struct firmware *fw,
                                         void *context))
{
	struct rom_firmware_work *fw_work = kzalloc(sizeof(*fw_work), gfp);

	if (!fw_work)
		return -ENOMEM;

	fw_work->name = kstrdup_const(name, gfp);
	if (!fw_work->name) {
		kfree(fw_work);
		return -ENOMEM;
	}

	fw_work->device  = device;
	fw_work->context = context;
	fw_work->cont    = cont;

	INIT_WORK(&fw_work->work, rom_firmware_work_func);
	schedule_work(&fw_work->work);

	return 0;
}
