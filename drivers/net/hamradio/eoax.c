/*
 * Ethernet over AX.25 encapsulation Linux kernel driver.
 * Copyright (C) 2016  Antony Chazapis SV1OAN (chazapis@gmail.com)
 *
 * Based on bpqether.c, by Joerg DL1BKE.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>

#include <net/ip.h>
#include <net/arp.h>
#include <net/net_namespace.h>

#define AX25_P_EOAX 0xD0

static int eoax_rcv(struct sk_buff *, struct net_device *);
static int eoax_device_event(struct notifier_block *, unsigned long, void *);

static struct ax25_protocol eoax_pid = {
	.pid		= AX25_P_EOAX,
	.func		= NULL,
	.ui_func	= eoax_rcv,
};

static struct notifier_block eoax_dev_notifier = {
	.notifier_call = eoax_device_event,
};

struct eoaxdev {
	struct list_head eoax_list;	/* list of eoax devices chain */
	struct net_device *axdev;	/* link to AX.25 device */
	struct net_device *ethdev;	/* link to ethernet device (eoax#) */
};

static LIST_HEAD(eoax_devices);

// /*
//  * bpqether network devices are paired with ethernet devices below them, so
//  * form a special "super class" of normal ethernet devices; split their locks
//  * off into a separate class since they always nest.
//  */
// static struct lock_class_key bpq_netdev_xmit_lock_key;
// static struct lock_class_key bpq_netdev_addr_lock_key;

// static void bpq_set_lockdep_class_one(struct net_device *dev,
// 				      struct netdev_queue *txq,
// 				      void *_unused)
// {
// 	lockdep_set_class(&txq->_xmit_lock, &bpq_netdev_xmit_lock_key);
// }

// static void bpq_set_lockdep_class(struct net_device *dev)
// {
// 	lockdep_set_class(&dev->addr_list_lock, &bpq_netdev_addr_lock_key);
// 	netdev_for_each_tx_queue(dev, bpq_set_lockdep_class_one, NULL);
// }

// /* ------------------------------------------------------------------------ */


/*
 *	Get the AX.25 device for an eoax device.
 */
static inline struct net_device *eoax_get_ax25_dev(struct net_device *dev)
{
	struct eoaxdev *eoax = netdev_priv(dev);

	return eoax ? eoax->axdev : NULL;
}

/*
 *	Get the eoax device for the AX.25 device.
 */
static inline struct net_device *eoax_get_ether_dev(struct net_device *dev)
{
	struct eoaxdev *eoax;

	list_for_each_entry_rcu(eoax, &eoax_devices, eoax_list) {
		if (eoax->axdev == dev)
			return eoax->ethdev;
	}
	return NULL;
}

/* ------------------------------------------------------------------------ */


static void dump_skb(struct sk_buff *skb)
{
	unsigned char *ptr;
	int i;

	// printk(KERN_INFO "eoax: skb->head = %p, skb->data = %p, skb->tail = %d\n", skb->head, skb->data, skb->tail);
	printk(KERN_INFO "eoax: skb->len = %d\n", skb->len);
	ptr = skb->data;
	for (i = 0; i < skb->len; i++) {
		if (i % 16 == 0)
			printk(KERN_INFO "eoax: ");			
		printk("0x%2.2x", *ptr++);
		if ((i + 1) % 16 == 0)
			printk("\n");
		else
			printk(" ");
	}
	if (i % 16 != 0)
		printk("\n");
}

static void ax25_address_to_eth(unsigned char *ax25_addr,
				unsigned char *eth_addr)
{
	unsigned char ssid;

	memcpy(eth_addr, ax25_addr, ETH_ALEN);
	ssid = ax25_addr[6];
	eth_addr[0] |= (ssid >> 4) & 1;
	eth_addr[1] |= (ssid >> 3) & 1;
	eth_addr[2] |= (ssid >> 2) & 1;
	eth_addr[3] |= (ssid >> 1) & 1;
}

static void eth_address_to_ax25(unsigned char *eth_addr,
				unsigned char *ax25_addr)
{
	unsigned char ssid;

	memcpy(ax25_addr, eth_addr, ETH_ALEN);
	ssid = 0;
	ssid |= (eth_addr[0] & 1) << 4;
	ssid |= (eth_addr[1] & 1) << 3;
	ssid |= (eth_addr[2] & 1) << 2;
	ssid |= (eth_addr[3] & 1) << 1;
	ax25_addr[6] = ssid;
}

/*
 *	Receive an ethernet frame via an AX.25 interface.
 */
static int eoax_rcv(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	// char * ptr;
	// struct ethhdr *eth;
	// struct eoaxdev *eoax;

	printk(KERN_INFO "eoax: start rcv\n");
	dump_skb(skb);

	if (!net_eq(dev_net(dev), &init_net))
		goto drop;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		return NET_RX_DROP;

	// if (!pskb_may_pull(skb, sizeof(struct ethhdr)))
	// 	goto drop;

	rcu_read_lock();
	dev = eoax_get_ether_dev(dev);

	if (dev == NULL || !netif_running(dev)) 
		goto drop_unlock;

	/*
	 * if we want to accept frames from just one ethernet device
	 * we check the source address of the sender.
	 */

	// eoax = netdev_priv(dev);

	// eth = eth_hdr(skb);

	// if (!(eoax->acpt_addr[0] & 0x01) &&
	//     !ether_addr_equal(eth->h_source, eoax->acpt_addr))
	// 	goto drop_unlock;

	// if (skb_cow(skb, sizeof(struct ethhdr)))
	// 	goto drop_unlock;

	len = skb->data[0] + skb->data[1] * 256 - 5;

	printk(KERN_INFO "eoax: trimming packet from %d to %d\n", skb->len, len);

	skb_pull(skb, 2);	/* Remove the length bytes */
	skb_trim(skb, len);	/* Set the length of the data */

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

	// ptr = skb_push(skb, 1);
	// *ptr = 0;

	// skb_reset_transport_header(skb);
	// skb_reset_network_header(skb);
	skb->protocol = eth_type_trans(skb, dev);
	printk(KERN_INFO "eoax: end rcv\n");
	dump_skb(skb);
	netif_rx(skb);
unlock:

	rcu_read_unlock();

	return 0;
drop_unlock:
	kfree_skb(skb);
	goto unlock;

drop:
	kfree_skb(skb);
	return 0;
}

/*
 * 	Send an ethernet frame via an AX.25 interface.
 */
static netdev_tx_t eoax_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned char *ptr;
	struct eoaxdev *eoax;
	struct net_device *orig_dev;
	int size;

	struct ethhdr *eth;
	unsigned char dest[AX25_ADDR_LEN];

	printk(KERN_INFO "eoax: start xmit\n");
	dump_skb(skb);

	/*
	 * Just to be *really* sure not to send anything if the interface
	 * is down, the ethernet device may have gone.
	 */
	if (!netif_running(dev)) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	eth = eth_hdr(skb);

	size = skb->len;

	/*
	 * We're about to mess with the skb which may still shared with the
	 * generic networking code so unshare and ensure it's got enough
	 * space for the BPQ headers.
	 */
	if (skb_cow(skb, AX25_HEADER_LEN + 2)) {
		if (net_ratelimit())
			pr_err("eoax: out of memory\n");
		kfree_skb(skb);

		return NETDEV_TX_OK;
	}

	ptr = skb_push(skb, 2);			/* Make space for length */

	*ptr++ = (size + 5) % 256;
	*ptr++ = (size + 5) / 256;

	// printk(KERN_INFO "eoax: adding len 0x%2.2x 0x%2.2x\n", (size + 5) % 256, (size + 5) / 256);
	// dump_skb(skb);

	eoax = netdev_priv(dev);

	orig_dev = dev;
	if ((dev = eoax_get_ax25_dev(dev)) == NULL) {
		orig_dev->stats.tx_dropped++;
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	skb->dev = dev;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	if (unlikely(ether_addr_equal_64bits(eth->h_dest, orig_dev->broadcast))) {
		skb->pkt_type = PACKET_BROADCAST;
		memcpy(dest, ax25_bcast.ax25_call, AX25_ADDR_LEN);
	// } else if (unlikely(!ether_addr_equal_64bits(eth->h_dest, dev->dev_addr))) {
	// 	skb->pkt_type = PACKET_OTHERHOST;
	} else {
		skb->pkt_type = PACKET_HOST;
		eth_address_to_ax25(eth->h_dest, dest);
	}
	skb->pkt_type = PACKET_HOST;
	skb->protocol = eth->h_proto;
	// skb->protocol = eth_type_trans(skb, dev);	
	// printk(KERN_INFO "eoax: after eth_type_trans\n");
	// dump_skb(skb);
	// skb_reset_network_header(skb);
	dev_hard_header(skb, dev, AX25_P_EOAX, dest, NULL, 0);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
  
	printk(KERN_INFO "eoax: end xmit\n");
	dump_skb(skb);

	dev_queue_xmit(skb);
	netif_wake_queue(dev);
	return NETDEV_TX_OK;
}

static int eoax_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int eoax_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

/* ------------------------------------------------------------------------ */


static const struct net_device_ops eoax_netdev_ops = {
	.ndo_open	     = eoax_open,
	.ndo_stop	     = eoax_close,
	.ndo_start_xmit	     = eoax_xmit,
};

static void eoax_setup(struct net_device *dev)
{
	ether_setup(dev);

	// dev->header_ops		= &eth_header_ops;
	// dev->type		= ARPHRD_ETHER;
	dev->hard_header_len	= ETH_HLEN + AX25_HEADER_LEN + 2;
	dev->mtu		= AX25_DEF_PACLEN;
	// dev->addr_len		= ETH_ALEN;
	dev->tx_queue_len	= 0;
	dev->flags		= IFF_BROADCAST;

	eth_broadcast_addr(dev->broadcast);
	// eth_zero_addr(dev->dev_addr);

	dev->netdev_ops		= &eoax_netdev_ops;
	dev->destructor		= free_netdev;
}

/*
 *	Setup a new device.
 */
static int eoax_new_device(struct net_device *axdev)
{
	int err;
	struct net_device *ndev;
	struct eoaxdev *eoax;

	ndev = alloc_netdev(sizeof(struct eoaxdev), "dummy%d", NET_NAME_UNKNOWN, eoax_setup);
	if (!ndev)
		return -ENOMEM;

	eoax = netdev_priv(ndev);
	dev_hold(axdev);
	eoax->axdev = axdev;
	eoax->ethdev = ndev;

	ndev->mtu = axdev->mtu - (ETH_HLEN + 2);
	ax25_address_to_eth(axdev->dev_addr, ndev->dev_addr);

	err = register_netdevice(ndev);
	if (err)
		goto error;
	// eoax_set_lockdep_class(ndev);

	/* List protected by RTNL */
	list_add_rcu(&eoax->eoax_list, &eoax_devices);

	printk(KERN_INFO "eoax: registered new device %s over %s\n", eoax->ethdev->name, eoax->axdev->name);

	return 0;

 error:
	dev_put(axdev);
	free_netdev(ndev);
	return err;
}

static void eoax_free_device(struct net_device *ndev)
{
	struct eoaxdev *eoax = netdev_priv(ndev);
	struct net_device *dev;

	printk(KERN_INFO "eoax: unregistered device %s\n", eoax->ethdev->name);

	if ((dev = eoax_get_ax25_dev(ndev)) != NULL) {
		eoax->axdev->header_ops = &ax25_header_ops;
	}

	dev_put(eoax->axdev);
	list_del_rcu(&eoax->eoax_list);

	unregister_netdevice(ndev);
}

/*
 *	Handle device status changes.
 */
static int eoax_device_event(struct notifier_block *this,
			     unsigned long event,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (dev->type != ARPHRD_AX25)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:		/* new AX.25 device -> new eoax interface */
		if (eoax_get_ether_dev(dev) == NULL)
			eoax_new_device(dev);
		break;

	case NETDEV_DOWN:	/* AX.25 device closed -> close eoax interface */
		if ((dev = eoax_get_ether_dev(dev)) != NULL)
			dev_close(dev);
		break;

	case NETDEV_UNREGISTER:	/* AX.25 device removed -> free eoax interface */
		if ((dev = eoax_get_ether_dev(dev)) != NULL)
			eoax_free_device(dev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}


/* ------------------------------------------------------------------------ */

static int __init eoax_init_driver(void)
{
	printk(KERN_INFO "eoax: Ethernet over AX.25 emulation\n");

	ax25_register_pid(&eoax_pid);

	register_netdevice_notifier(&eoax_dev_notifier);

	return 0;
}

static void __exit eoax_cleanup_driver(void)
{
	struct eoaxdev *eoax;

	ax25_protocol_release(AX25_P_EOAX);

	unregister_netdevice_notifier(&eoax_dev_notifier);

	rtnl_lock();
	while (!list_empty(&eoax_devices)) {
		eoax = list_entry(eoax_devices.next, struct eoaxdev, eoax_list);
		eoax_free_device(eoax->ethdev);
	}
	rtnl_unlock();
}

MODULE_AUTHOR("Antony Chazapis SV1OAN <chazapis@gmail.com>");
MODULE_DESCRIPTION("Ethernet over AX.25 emulation");
MODULE_LICENSE("GPL");

module_init(eoax_init_driver);
module_exit(eoax_cleanup_driver);
