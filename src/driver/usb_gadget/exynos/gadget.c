/*
 * \brief  Linux side of the gadget driver: one composite gadget whose
 *         functions are the Usb_gadget sessions
 * \author Ivan Loskutov
 * \date   2026-10-23
 *
 * The composite gadget is registered once every <function> slot has a
 * session that described its function, and unregistered when one goes
 * away. Each slot is a 'usb_function': its bind takes interface and
 * string numbers from composite and endpoints from the UDC, and places
 * the session's descriptors with them (gadget_compose). Class and vendor
 * requests go to the session as SETUP events and are answered
 * asynchronously on ep0, as f_fs does. Packets become usb_requests on the
 * session's DMA buffer.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include <lx_emul/task.h>

#include <exynos_glue.h>
#include <gadget_api.h>

/* as drivers/usb/dwc3/gadget.h, whose name clashes with ours */
#define gadget_to_dwc(g) (dev_get_platdata(&(g)->dev))

struct slot_function
{
	struct usb_function f;
	unsigned            slot;

	unsigned char       interfaces;
	unsigned char       first_interface;
	unsigned            interface_enabled;   /* bit per local interface */
	int                 enabled;

	unsigned            ep_count;
	struct usb_ep      *ep[GADGET_MAX_EPS];
	unsigned char       ep_interface[GADGET_MAX_EPS];
	int                 ep_enabled[GADGET_MAX_EPS];

	struct usb_string          strings[GADGET_MAX_STRINGS + 1];
	struct usb_gadget_strings  strtab;
	struct usb_gadget_strings *strtabs[2];

	unsigned char                 *desc_buf[3];
	struct usb_descriptor_header **desc[3];

	struct usb_request     *ep0_req;
	int                     ep0_queued;
	int                     setup_pending;
	struct usb_ctrlrequest  setup;
};

struct packet_ctx
{
	unsigned      slot;
	unsigned long tag;
};

static struct slot_function *functions[GADGET_MAX_SLOTS];
static int                   registered;


static enum gadget_speed speed_of(struct usb_gadget *gadget)
{
	switch (gadget->speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS: return GADGET_SS;
	case USB_SPEED_HIGH:       return GADGET_HS;
	default:                   return GADGET_FS;
	}
}


static enum gadget_status status_of(int status)
{
	switch (status) {
	case 0:          return GADGET_OK;
	case -EPIPE:     return GADGET_HALTED;
	default:         return GADGET_FLUSHED;  /* -ESHUTDOWN, -ECONNRESET, ... */
	}
}


/*
 * Descriptor helpers
 */

static void for_each_descriptor(unsigned char const *p, unsigned len,
                                void (*fn)(unsigned char const *, void *), void *data)
{
	unsigned o;
	for (o = 0; o + 2 <= len && p[o] >= 2 && o + p[o] <= len; o += p[o])
		fn(p + o, data);
}


struct layout { struct slot_function *sf; int interface; };

static void collect_layout(unsigned char const *d, void *data)
{
	struct layout *l = data;
	struct slot_function *sf = l->sf;

	if (d[1] == USB_DT_INTERFACE) {
		l->interface = d[2];
		if (d[2] + 1 > sf->interfaces)
			sf->interfaces = d[2] + 1;
	}
	if (d[1] == USB_DT_ENDPOINT && sf->ep_count < GADGET_MAX_EPS)
		sf->ep_interface[sf->ep_count++] = l->interface;
}


struct split { struct usb_descriptor_header **array; unsigned count; };

static void count_descriptor(unsigned char const *d, void *data)
{
	((struct split *)data)->count++;
}


static void store_descriptor(unsigned char const *d, void *data)
{
	struct split *s = data;
	s->array[s->count++] = (struct usb_descriptor_header *)d;
}


/* NULL-terminated array of pointers into 'buf' */
static struct usb_descriptor_header **split_descriptors(unsigned char *buf, unsigned len)
{
	struct split s = { NULL, 0 };

	for_each_descriptor(buf, len, count_descriptor, &s);

	s.array = kcalloc(s.count + 1, sizeof(*s.array), GFP_KERNEL);
	if (!s.array)
		return NULL;

	s.count = 0;
	for_each_descriptor(buf, len, store_descriptor, &s);
	return s.array;
}


/*
 * Function operations
 */

static struct slot_function *to_sf(struct usb_function *f)
{
	return container_of(f, struct slot_function, f);
}


static int fn_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct slot_function *sf = to_sf(f);
	struct usb_composite_dev *cdev = c->cdev;
	unsigned char ep_address[GADGET_MAX_EPS];
	unsigned char string_base = 0;
	unsigned char const *fs, *ss;
	unsigned fs_len, ss_len, i, n, s;
	struct layout layout = { sf, -1 };

	fs = gadget_descriptors(sf->slot, GADGET_FS, &fs_len);
	ss = gadget_descriptors(sf->slot, GADGET_SS, &ss_len);

	sf->interfaces = 0;
	sf->ep_count   = 0;
	for_each_descriptor(fs, fs_len, collect_layout, &layout);

	for (i = 0; i < sf->interfaces; i++) {
		int const id = usb_interface_id(c, f);
		if (id < 0)
			return id;
		if (i == 0)
			sf->first_interface = id;
	}

	/* copies: the session may go while the gadget is still bound */
	n = gadget_string_count(sf->slot);
	if (n) {
		for (i = 0; i < n; i++) {
			sf->strings[i].s = kstrdup(gadget_string(sf->slot, i), GFP_KERNEL);
			if (!sf->strings[i].s)
				return -ENOMEM;
		}
		sf->strings[n].s = NULL;

		if (usb_string_ids_tab(cdev, sf->strings))
			return -EINVAL;

		string_base   = sf->strings[0].id - 1;
		sf->strtab    = (struct usb_gadget_strings) { .language = 0x0409,
		                                              .strings  = sf->strings };
		sf->strtabs[0] = &sf->strtab;
		sf->strtabs[1] = NULL;
		f->strings     = sf->strtabs;
	}

	/* endpoints from the UDC, matched on the full-speed descriptors */
	{
		unsigned o, e = 0, so = 0;
		for (o = 0; o + 2 <= fs_len && fs[o] >= 2; o += fs[o]) {
			struct usb_endpoint_descriptor desc = { };
			struct usb_ss_ep_comp_descriptor const *comp = NULL;
			struct usb_ep *ep;

			if (fs[o + 1] != USB_DT_ENDPOINT || e >= sf->ep_count)
				continue;

			memcpy(&desc, fs + o, USB_DT_ENDPOINT_SIZE);

			/* the companion of the e-th endpoint in the SS descriptors */
			for (; so + 2 <= ss_len && ss[so] >= 2; so += ss[so]) {
				if (ss[so + 1] != USB_DT_ENDPOINT) continue;
				if (so + ss[so] + 2 <= ss_len && ss[so + ss[so] + 1] == USB_DT_SS_ENDPOINT_COMP)
					comp = (void const *)(ss + so + ss[so]);
				so += ss[so];
				break;
			}

			ep = usb_ep_autoconfig_ss(cdev->gadget, &desc,
			                          (struct usb_ss_ep_comp_descriptor *)comp);
			if (!ep) {
				pr_err("usb_gadget: function %u: no endpoint for local %u\n",
				       sf->slot, e + 1);
				return -ENODEV;
			}
			ep->driver_data = sf;
			sf->ep[e]       = ep;
			ep_address[e]   = desc.bEndpointAddress;
			e++;
		}
	}

	for (s = GADGET_FS; s <= GADGET_SS; s++) {
		unsigned len;
		sf->desc_buf[s] = kmalloc(GADGET_BLOB_MAX, GFP_KERNEL);
		if (!sf->desc_buf[s])
			return -ENOMEM;
		len = gadget_compose(sf->slot, s, sf->first_interface, string_base,
		                     ep_address, sf->ep_count, sf->desc_buf[s]);
		if (!len) {
			pr_err("usb_gadget: function %u: composing descriptors failed\n", sf->slot);
			return -EINVAL;
		}
		sf->desc[s] = split_descriptors(sf->desc_buf[s], len);
		if (!sf->desc[s])
			return -ENOMEM;
	}

	f->fs_descriptors  = sf->desc[GADGET_FS];
	f->hs_descriptors  = sf->desc[GADGET_HS];
	f->ss_descriptors  = sf->desc[GADGET_SS];
	f->ssp_descriptors = sf->desc[GADGET_SS];

	sf->ep0_req = usb_ep_alloc_request(cdev->gadget->ep0, GFP_KERNEL);
	if (!sf->ep0_req)
		return -ENOMEM;

	pr_info("usb_gadget: function %u bound: interfaces %u.., %u endpoints\n",
	        sf->slot, sf->first_interface, sf->ep_count);
	return 0;
}


static void fn_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct slot_function *sf = to_sf(f);
	unsigned s;

	if (sf->ep0_req)
		usb_ep_free_request(c->cdev->gadget->ep0, sf->ep0_req);
	sf->ep0_req = NULL;

	for (s = GADGET_FS; s <= GADGET_SS; s++) {
		kfree(sf->desc[s]);
		kfree(sf->desc_buf[s]);
		sf->desc[s] = NULL;
		sf->desc_buf[s] = NULL;
	}

	for (s = 0; s < GADGET_MAX_STRINGS; s++) {
		kfree(sf->strings[s].s);
		sf->strings[s].s = NULL;
	}
	f->fs_descriptors = f->hs_descriptors = NULL;
	f->ss_descriptors = f->ssp_descriptors = NULL;
}


static void disable_endpoints(struct slot_function *sf)
{
	unsigned i;

	/* completes the requests in flight, they are acked FLUSHED */
	for (i = 0; i < sf->ep_count; i++)
		if (sf->ep_enabled[i]) {
			usb_ep_disable(sf->ep[i]);
			sf->ep_enabled[i] = 0;
		}
}


static int fn_set_alt(struct usb_function *f, unsigned interface, unsigned alt)
{
	struct slot_function *sf = to_sf(f);
	struct usb_gadget *gadget = f->config->cdev->gadget;
	unsigned const local = interface - sf->first_interface;
	unsigned i;

	if (alt)
		return -EINVAL;

	for (i = 0; i < sf->ep_count; i++) {
		int err;

		if (sf->ep_interface[i] != local)
			continue;

		if (sf->ep_enabled[i]) {
			usb_ep_disable(sf->ep[i]);
			sf->ep_enabled[i] = 0;
		}
		err = config_ep_by_speed(gadget, f, sf->ep[i]);
		if (!err)
			err = usb_ep_enable(sf->ep[i]);
		if (err) {
			pr_err("usb_gadget: function %u: enabling endpoint %u failed: %d\n",
			       sf->slot, i + 1, err);
			return err;
		}
		sf->ep_enabled[i] = 1;
	}

	sf->interface_enabled |= 1u << local;

	if (!sf->enabled && sf->interface_enabled == (1u << sf->interfaces) - 1) {
		sf->enabled = 1;
		pr_info("usb_gadget: configured, %s, function %u enabled\n",
		        usb_speed_string(gadget->speed), sf->slot);
		gadget_event(sf->slot, GADGET_ENABLED, speed_of(gadget), NULL);
	}
	return 0;
}


static void fn_disable(struct usb_function *f)
{
	struct slot_function *sf = to_sf(f);

	disable_endpoints(sf);
	sf->interface_enabled = 0;
	sf->setup_pending     = 0;

	if (sf->enabled) {
		sf->enabled = 0;
		pr_info("usb_gadget: function %u disabled\n", sf->slot);
		gadget_event(sf->slot, GADGET_DISABLED, speed_of(f->config->cdev->gadget), NULL);
	}
}


static int fn_setup(struct usb_function *f, struct usb_ctrlrequest const *ctrl)
{
	struct slot_function *sf = to_sf(f);
	struct usb_ctrlrequest local = *ctrl;
	u16 const index = le16_to_cpu(ctrl->wIndex);

	/* wIndex function-local */
	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_INTERFACE:
		local.wIndex = cpu_to_le16((index & 0xff00) | ((index & 0xff) - sf->first_interface));
		break;
	case USB_RECIP_ENDPOINT:
	{
		unsigned i;
		for (i = 0; i < sf->ep_count; i++)
			if (sf->ep[i]->address == (index & 0xff))
				local.wIndex = cpu_to_le16((index & 0xff80) | (i + 1));
		break;
	}
	default:
		break;
	}

	sf->setup         = *ctrl;
	sf->setup_pending = 1;

	gadget_event(sf->slot, GADGET_SETUP, speed_of(f->config->cdev->gadget),
	             (unsigned char const *)&local);
	gadget_wakeup();

	/* no data stage: hold the status stage until the function answers */
	return le16_to_cpu(ctrl->wLength) ? 0 : USB_GADGET_DELAYED_STATUS;
}


static void fn_suspend(struct usb_function *f)
{
	struct slot_function *sf = to_sf(f);
	gadget_event(sf->slot, GADGET_SUSPEND, speed_of(f->config->cdev->gadget), NULL);
}


static void fn_resume(struct usb_function *f)
{
	struct slot_function *sf = to_sf(f);
	gadget_event(sf->slot, GADGET_RESUME, speed_of(f->config->cdev->gadget), NULL);
}


/*
 * Packets
 */

static void request_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct packet_ctx *ctx = req->context;

	gadget_ack(ctx->slot, ctx->tag, status_of(req->status), req->actual);
	kfree(ctx);
	usb_ep_free_request(ep, req);
	gadget_wakeup();
}


static void ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct packet_ctx *ctx = req->context;
	struct slot_function *sf = functions[ctx->slot];

	if (sf)
		sf->ep0_queued = 0;

	gadget_ack(ctx->slot, ctx->tag, status_of(req->status), req->actual);
	kfree(ctx);
	gadget_wakeup();
}


static void ep0_packet(struct slot_function *sf, struct gadget_packet const *p)
{
	struct usb_gadget *gadget = sf->f.config->cdev->gadget;
	u16 const w_length = le16_to_cpu(sf->setup.wLength);
	int const in = sf->setup.bRequestType & USB_DIR_IN;
	struct packet_ctx *ctx;
	int err;

	/* no request, or one answer still on its way: nothing to do with it */
	if (!sf->setup_pending || sf->ep0_queued) {
		gadget_ack(sf->slot, p->tag, GADGET_FLUSHED, 0);
		return;
	}

	if (p->stall) {
		usb_ep_set_halt(gadget->ep0);
		sf->setup_pending = 0;
		gadget_ack(sf->slot, p->tag, GADGET_STALL, 0);
		return;
	}

	/* the status stage after an OUT data stage is the controller's */
	if (w_length && !in && !p->len) {
		sf->setup_pending = 0;
		gadget_ack(sf->slot, p->tag, GADGET_OK, 0);
		return;
	}

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		gadget_ack(sf->slot, p->tag, GADGET_FLUSHED, 0);
		return;
	}
	*ctx = (struct packet_ctx) { sf->slot, p->tag };

	sf->ep0_req->buf      = p->buf;
	sf->ep0_req->length   = min_t(unsigned, p->len, w_length);
	sf->ep0_req->zero     = in && p->len < w_length;
	sf->ep0_req->context  = ctx;
	sf->ep0_req->complete = ep0_complete;

	/* data IN or no data: answered; data OUT: wait for the status packet */
	if (in || !w_length)
		sf->setup_pending = 0;

	sf->ep0_queued = 1;
	err = usb_ep_queue(gadget->ep0, sf->ep0_req, GFP_KERNEL);
	if (err) {
		pr_err("usb_gadget: function %u: ep0 queue failed: %d\n", sf->slot, err);
		sf->ep0_queued = 0;
		kfree(ctx);
		gadget_ack(sf->slot, p->tag, GADGET_FLUSHED, 0);
	}
}


static void data_packet(struct slot_function *sf, struct gadget_packet const *p)
{
	struct usb_ep *ep;
	struct usb_request *req;
	struct packet_ctx *ctx;

	if (!sf->enabled || p->endpoint > sf->ep_count || !sf->ep_enabled[p->endpoint - 1]) {
		gadget_ack(sf->slot, p->tag, GADGET_FLUSHED, 0);
		return;
	}

	ep  = sf->ep[p->endpoint - 1];
	req = usb_ep_alloc_request(ep, GFP_KERNEL);
	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!req || !ctx) {
		if (req) usb_ep_free_request(ep, req);
		kfree(ctx);
		gadget_ack(sf->slot, p->tag, GADGET_FLUSHED, 0);
		return;
	}

	*ctx = (struct packet_ctx) { sf->slot, p->tag };

	req->buf      = p->buf;
	req->length   = p->len;
	req->zero     = p->zlp;
	req->context  = ctx;
	req->complete = request_complete;

	if (usb_ep_queue(ep, req, GFP_KERNEL)) {
		usb_ep_free_request(ep, req);
		kfree(ctx);
		gadget_ack(sf->slot, p->tag, GADGET_FLUSHED, 0);
	}
}


static void handle_packets(unsigned slot)
{
	struct gadget_packet p;

	while (gadget_next_packet(slot, &p)) {
		struct slot_function *sf = registered ? functions[slot] : NULL;

		if (!sf || !sf->f.config) {
			gadget_ack(slot, p.tag, GADGET_FLUSHED, 0);
			continue;
		}
		if (p.endpoint == 0)
			ep0_packet(sf, &p);
		else
			data_packet(sf, &p);
	}
}


/*
 * Composite gadget
 */

static struct usb_device_descriptor device_desc = {
	.bLength            = sizeof(device_desc),
	.bDescriptorType    = USB_DT_DEVICE,
	.bDeviceClass       = USB_CLASS_MISC,   /* with interface associations */
	.bDeviceSubClass    = 0x02,
	.bDeviceProtocol    = 0x01,
	.bNumConfigurations = 1,
};

enum { STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL };

static struct usb_string dev_strings[] = {
	[STR_MANUFACTURER] = { },
	[STR_PRODUCT]      = { },
	[STR_SERIAL]       = { },
	{ }
};

static struct usb_gadget_strings dev_strtab = {
	.language = 0x0409,
	.strings  = dev_strings,
};

static struct usb_gadget_strings *dev_strtabs[] = { &dev_strtab, NULL };

static struct usb_configuration configuration = {
	.label               = "genode",
	.bConfigurationValue = 1,
	.bmAttributes        = USB_CONFIG_ATT_ONE,
};


static int config_bind(struct usb_configuration *c)
{
	unsigned i;

	for (i = 0; i < gadget_slot_count(); i++) {
		struct slot_function *sf;
		int err;

		/*
		 * The function of an earlier binding: composite reads it after
		 * its unbind (usb_remove_function), so it is freed only here
		 */
		kfree(functions[i]);
		functions[i] = NULL;

		sf = kzalloc(sizeof(*sf), GFP_KERNEL);
		if (!sf)
			return -ENOMEM;

		sf->slot         = i;
		sf->f.name       = "genode";
		sf->f.bind       = fn_bind;
		sf->f.unbind     = fn_unbind;
		sf->f.set_alt    = fn_set_alt;
		sf->f.disable    = fn_disable;
		sf->f.setup      = fn_setup;
		sf->f.suspend    = fn_suspend;
		sf->f.resume     = fn_resume;
		functions[i]     = sf;

		err = usb_add_function(c, &sf->f);
		if (err) {
			functions[i] = NULL;
			kfree(sf);
			return err;
		}
	}
	return 0;
}


static int genode_composite_bind(struct usb_composite_dev *cdev)
{
	struct gadget_identity id;
	int err;

	gadget_identity(&id);

	device_desc.idVendor  = cpu_to_le16(id.vendor_id);
	device_desc.idProduct = cpu_to_le16(id.product_id);
	device_desc.bcdDevice = cpu_to_le16(0x0100);

	dev_strings[STR_MANUFACTURER].s = id.manufacturer;
	dev_strings[STR_PRODUCT].s      = id.product;
	dev_strings[STR_SERIAL].s       = id.serial;

	err = usb_string_ids_tab(cdev, dev_strings);
	if (err)
		return err;

	device_desc.iManufacturer = dev_strings[STR_MANUFACTURER].id;
	device_desc.iProduct      = dev_strings[STR_PRODUCT].id;
	device_desc.iSerialNumber = dev_strings[STR_SERIAL].id;

	configuration.MaxPower = id.max_power_ma;

	/* the vendor's register setup, before the UDC core connects */
	exynos_dwc3_config(gadget_to_dwc(cdev->gadget));

	err = usb_add_config(cdev, &configuration, config_bind);
	if (err)
		return err;

	pr_info("usb_gadget: gadget bound, %u function(s)\n", gadget_slot_count());
	return 0;
}


static int genode_composite_unbind(struct usb_composite_dev *cdev)
{
	return 0;
}


static void genode_composite_disconnect(struct usb_composite_dev *cdev)
{
	pr_info("usb_gadget: disconnect\n");
}


static struct usb_composite_driver composite = {
	.name       = "genode_gadget",
	.dev        = &device_desc,
	.strings    = dev_strtabs,
	.max_speed  = USB_SPEED_SUPER_PLUS,
	.bind       = genode_composite_bind,
	.unbind     = genode_composite_unbind,
	.disconnect = genode_composite_disconnect,
};


/*
 * Task serving the sessions
 */

static struct task_struct *gadget_task;


void gadget_wakeup(void)
{
	if (gadget_task)
		lx_emul_task_unblock(gadget_task);
}


static int all_slots_ready(void)
{
	unsigned i, n = gadget_slot_count();

	for (i = 0; i < n; i++)
		if (!gadget_slot_ready(i))
			return 0;
	return n > 0;
}


static void release_slots(void)
{
	unsigned i;
	for (i = 0; i < gadget_slot_count(); i++)
		gadget_slot_released(i);
}


static int gadget_task_function(void *arg)
{
	for (;;) {
		unsigned i;
		int const ready = all_slots_ready();

		if (ready && !registered) {
			int const err = usb_composite_probe(&composite);
			if (err)
				pr_err("usb_gadget: registering the gadget failed: %d\n", err);
			else
				registered = 1;
		}

		if (!ready && registered) {
			pr_info("usb_gadget: a function went away, gadget unbound\n");
			usb_composite_unregister(&composite);
			registered = 0;
		}

		/* no request of an unbound gadget is left on any buffer */
		if (!registered)
			release_slots();

		for (i = 0; i < gadget_slot_count(); i++)
			handle_packets(i);

		lx_emul_task_schedule(true);
	}
	return 0;
}


void usb_gadget_start_task(void)
{
	int const pid = kernel_thread(gadget_task_function, NULL,
	                              CLONE_FS | CLONE_FILES);
	gadget_task = find_task_by_pid_ns(pid, NULL);
}
