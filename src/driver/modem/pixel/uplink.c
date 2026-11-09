/*
 * \brief  Uplink of the modem's mobile data
 * \author Norman Feske
 * \author Christian Helmuth
 * \author Stefan Kalkowski
 * \author Ivan Loskutov
 * \date   2024-10-15
 *
 * Derived from the following file of the Genode tree, for the one network
 * interface of the cpif that the driver's config names (the 'rmnetN' of the
 * data context), with socket buffers that may be non-linear, and IPv6:
 *
 *   repos/dde_linux/src/lib/lx_emul/nic.c
 *
 * The interfaces of the cpif carry raw IP packets. Towards the Uplink
 * session each packet gets an Ethernet header, destined to the session's
 * MAC address, which is what the NIC router needs at a domain with
 * 'use_arp: no'. Packets from the session lose the header; anything but
 * IPv4 and IPv6 is dropped.
 */

/*
 * Copyright (C) 2024-2026 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <lx_emul/task.h>
#include <genode_c_api/uplink.h>

#include "uplink.h"


/* locally administered, the interfaces of the cpif have none */
static unsigned char const _mac[ETH_ALEN] = { 0x02, 0x00, 0x5e, 0x00, 0x00, 0x01 };

static char                  _wanted[IFNAMSIZ];   /* the config's interface, "" for none */
static struct net_device    *_dev;
static struct genode_uplink *_uplink;
static struct task_struct   *_task;


/*
 * Packets of the interface to the session
 */

struct genode_uplink_tx_packet_context { struct sk_buff *skb; };


static unsigned long uplink_tx_packet_content(struct genode_uplink_tx_packet_context *ctx,
                                              char *dst, unsigned long dst_len)
{
	struct sk_buff * const skb = ctx->skb;
	struct ethhdr  * const eth = (struct ethhdr *)dst;
	unsigned char version;

	if (dst_len < ETH_HLEN + skb->len || skb->len < 1
	 || skb_copy_bits(skb, 0, &version, 1))
		return 0;

	version >>= 4;
	if (version != 4 && version != 6)
		return 0;

	memcpy(eth->h_dest, _mac, ETH_ALEN);
	eth_zero_addr(eth->h_source);
	eth->h_proto = htons(version == 4 ? ETH_P_IP : ETH_P_IPV6);

	/* the packet processing may hand out paged buffers */
	if (skb_copy_bits(skb, 0, dst + ETH_HLEN, skb->len))
		return 0;

	return ETH_HLEN + skb->len;
}


static rx_handler_result_t handle_rx(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct genode_uplink_tx_packet_context ctx = { .skb = skb };

	if (!_uplink || skb->dev != _dev)
		return RX_HANDLER_PASS;

	if (!genode_uplink_tx_packet(_uplink, uplink_tx_packet_content, &ctx))
		printk("modem: uplink: packet of %u bytes dropped\n", skb->len);

	kfree_skb(skb);
	return RX_HANDLER_CONSUMED;
}


/*
 * Packets of the session to the interface
 */

struct genode_uplink_rx_context { struct net_device *dev; };


static genode_uplink_rx_result_t uplink_rx_one_packet(struct genode_uplink_rx_context *ctx,
                                                      char const *ptr, unsigned long len)
{
	struct ethhdr const *eth = (struct ethhdr const *)ptr;
	struct sk_buff *skb;
	__be16 proto;

	if (len <= ETH_HLEN)
		return GENODE_UPLINK_RX_REJECTED;

	proto = eth->h_proto;
	if (proto != htons(ETH_P_IP) && proto != htons(ETH_P_IPV6))
		return GENODE_UPLINK_RX_REJECTED;   /* e.g. ARP: the link has none */

	skb = alloc_skb(len - ETH_HLEN, GFP_KERNEL);
	if (!skb)
		return GENODE_UPLINK_RX_RETRY;

	skb_put_data(skb, ptr + ETH_HLEN, len - ETH_HLEN);
	skb_reset_network_header(skb);
	skb->protocol = proto;
	skb->dev      = ctx->dev;

	if (dev_queue_xmit(skb) < 0)
		return GENODE_UPLINK_RX_REJECTED;

	return GENODE_UPLINK_RX_ACCEPTED;
}


/*
 * The interface and its session
 */

static void attach(void)
{
	struct net_device *dev = dev_get_by_name(&init_net, _wanted);

	if (!dev)
		return;   /* the cpif has not created it (yet), retried */

	rtnl_lock();

	/* one session packet per interface packet: no merged receive buffers */
	dev->features &= ~(NETIF_F_GRO | NETIF_F_GRO_UDP_FWD | NETIF_F_GRO_FRAGLIST);

	if (dev_open(dev, NULL) || netdev_rx_handler_register(dev, handle_rx, NULL)) {
		rtnl_unlock();
		dev_put(dev);
		printk("modem: uplink: cannot open %s\n", _wanted);
		_wanted[0] = 0;
		return;
	}
	rtnl_unlock();

	_dev = dev;
	printk("modem: uplink: %s opened\n", dev->name);
}


static void detach(void)
{
	if (_uplink) {
		genode_uplink_destroy(_uplink);
		_uplink = NULL;
	}

	rtnl_lock();
	netdev_rx_handler_unregister(_dev);
	dev_close(_dev);
	rtnl_unlock();

	printk("modem: uplink: %s closed\n", _dev->name);
	dev_put(_dev);
	_dev = NULL;
}


static void update_session(void)
{
	bool const carrier = netif_carrier_ok(_dev);

	if (carrier && !_uplink) {
		struct genode_uplink_args args;

		memset(&args, 0, sizeof(args));
		memcpy(args.mac_address, _mac, ETH_ALEN);
		args.label = _dev->name;

		_uplink = genode_uplink_create(&args);
		printk("modem: uplink: session for %s\n", _dev->name);
	}

	if (!carrier && _uplink) {
		genode_uplink_destroy(_uplink);
		_uplink = NULL;
	}
}


static int uplink_task(void *arg)
{
	for (;;) {

		if (_dev && strcmp(_dev->name, _wanted))
			detach();

		if (!_dev && _wanted[0])
			attach();

		if (_dev) {
			update_session();

			if (_uplink) {
				struct genode_uplink_rx_context ctx = { .dev = _dev };

				while (genode_uplink_rx(_uplink, uplink_rx_one_packet, &ctx));
			}
		}

		/* an interface not there yet is looked up again */
		if (_wanted[0] && !_dev)
			msleep(500);
		else
			lx_emul_task_schedule(true);
	}
	return 0;
}


void uplink_init(void)
{
	pid_t const pid = kernel_thread(uplink_task, NULL, CLONE_FS | CLONE_FILES);

	_task = find_task_by_pid_ns(pid, NULL);
}


void uplink_interface(char const *name)
{
	if (!strcmp(name, _wanted))
		return;

	strscpy(_wanted, name, sizeof(_wanted));
	uplink_handle_io();
}


void uplink_handle_io(void)
{
	if (_task)
		lx_emul_task_unblock(_task);
}
