/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

/* Bluetooth HCI connection handling. */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <linux/wakelock.h>

static int lockFlag;
static int connectionCount;

static void releaseWakeLock(void);
static void startWakeLockTimer(void);

static void hci_le_connect(struct hci_conn *conn)
{
        struct hci_dev *hdev = conn->hdev;
        struct hci_cp_le_create_conn cp;

        conn->state = BT_CONNECT;
        conn->out = 1;
        conn->link_mode |= HCI_LM_MASTER;
		conn->sec_level = BT_SECURITY_LOW;

        memset(&cp, 0, sizeof(cp));
        cp.scan_interval = cpu_to_le16(0x0004);
        cp.scan_window = cpu_to_le16(0x0004);
        bacpy(&cp.peer_addr, &conn->dst);
        cp.conn_interval_min = cpu_to_le16(0x0008);
        cp.conn_interval_max = cpu_to_le16(0x0100);
        cp.supervision_timeout = cpu_to_le16(0x03e8);
        cp.min_ce_len = cpu_to_le16(0x0001);
        cp.max_ce_len = cpu_to_le16(0x0001);

        hci_send_cmd(hdev, HCI_OP_LE_CREATE_CONN, sizeof(cp), &cp);
}

static void hci_le_connect_cancel(struct hci_conn *conn)
{
        hci_send_cmd(conn->hdev, HCI_OP_LE_CREATE_CONN_CANCEL, 0, NULL);
}


void hci_le_start_enc(struct hci_conn *conn, __le16 ediv, __u8 rand[8],
								__u8 ltk[16])
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_le_start_enc cp;

	BT_DBG("%p", conn);

	memset(&cp, 0, sizeof(cp));

	cp.handle = cpu_to_le16(conn->handle);
	memcpy(cp.ltk, ltk, sizeof(cp.ltk));
	cp.ediv = ediv;
	memcpy(cp.rand, rand, sizeof(rand));

	hci_send_cmd(hdev, HCI_OP_LE_START_ENC, sizeof(cp), &cp);
}
EXPORT_SYMBOL(hci_le_start_enc);

void hci_le_ltk_reply(struct hci_conn *conn, u8 ltk[16])
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_le_ltk_reply cp;

	BT_DBG("%p", conn);

	memset(&cp, 0, sizeof(cp));

	cp.handle = cpu_to_le16(conn->handle);
	memcpy(cp.ltk, ltk, sizeof(ltk));

	hci_send_cmd(hdev, HCI_OP_LE_LTK_REPLY, sizeof(cp), &cp);
}
EXPORT_SYMBOL(hci_le_ltk_reply);

void hci_le_ltk_neg_reply(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_le_ltk_neg_reply cp;

	BT_DBG("%p", conn);

	memset(&cp, 0, sizeof(cp));

	cp.handle = cpu_to_le16(conn->handle);

	hci_send_cmd(hdev, HCI_OP_LE_LTK_NEG_REPLY, sizeof(cp), &cp);
}


void hci_acl_connect(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct inquiry_entry *ie;
	struct hci_cp_create_conn cp;

	BT_DBG("%p", conn);

	conn->state = BT_CONNECT;
	conn->out = 1;

	conn->link_mode = HCI_LM_MASTER;

	conn->attempt++;

	conn->link_policy = hdev->link_policy;

	memset(&cp, 0, sizeof(cp));
	bacpy(&cp.bdaddr, &conn->dst);
	cp.pscan_rep_mode = 0x02;

	if ((ie = hci_inquiry_cache_lookup(hdev, &conn->dst))) {
		if (inquiry_entry_age(ie) <= INQUIRY_ENTRY_AGE_MAX) {
			cp.pscan_rep_mode = ie->data.pscan_rep_mode;
			cp.pscan_mode     = ie->data.pscan_mode;
			cp.clock_offset   = ie->data.clock_offset |
							cpu_to_le16(0x8000);
		}

		memcpy(conn->dev_class, ie->data.dev_class, 3);
		conn->ssp_mode = ie->data.ssp_mode;
	}

	cp.pkt_type = cpu_to_le16(conn->pkt_type);
	if (lmp_rswitch_capable(hdev) && !(hdev->link_mode & HCI_LM_MASTER))
		cp.role_switch = 0x01;
	else
		cp.role_switch = 0x00;

	hci_send_cmd(hdev, HCI_OP_CREATE_CONN, sizeof(cp), &cp);
}

static void hci_acl_connect_cancel(struct hci_conn *conn)
{
	struct hci_cp_create_conn_cancel cp;

	BT_DBG("%p", conn);

	if (conn->hdev->hci_ver < 2)
		return;

	bacpy(&cp.bdaddr, &conn->dst);
	hci_send_cmd(conn->hdev, HCI_OP_CREATE_CONN_CANCEL, sizeof(cp), &cp);
}

void hci_acl_disconn(struct hci_conn *conn, __u8 reason)
{
	struct hci_cp_disconnect cp;

	BT_DBG("%p", conn);

	conn->state = BT_DISCONN;

	cp.handle = cpu_to_le16(conn->handle);
	cp.reason = reason;
	hci_send_cmd(conn->hdev, HCI_OP_DISCONNECT, sizeof(cp), &cp);
}

void hci_add_sco(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_add_sco cp;

	BT_DBG("%p", conn);

	conn->state = BT_CONNECT;
	conn->out = 1;

	conn->attempt++;

	cp.handle   = cpu_to_le16(handle);
	cp.pkt_type = cpu_to_le16(conn->pkt_type);

	hci_send_cmd(hdev, HCI_OP_ADD_SCO, sizeof(cp), &cp);
}

void hci_setup_sync(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_setup_sync_conn cp;

	BT_DBG("%p", conn);

	conn->state = BT_CONNECT;
	conn->out = 1;

	conn->attempt++;

	BT_DBG("conn->pkt_type = %x", conn->pkt_type);
	cp.handle   = cpu_to_le16(handle);
	BT_DBG("remote features 0x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",
		conn->features[0], conn->features[1],
		conn->features[2], conn->features[3],
		conn->features[4], conn->features[5],
		conn->features[6], conn->features[7]);


	BT_DBG("local features 0x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",
		hdev->features[0], hdev->features[1],
		hdev->features[2], hdev->features[3],
		hdev->features[4], hdev->features[5],
		hdev->features[6], hdev->features[7]);

	if (lmp_esco_capable(hdev)) {
		if ((conn->features[5] & LMP_EDR_ESCO_2M) &&
						lmp_esco_capable(conn)) {
			BT_DBG("hci params for 2EV3/S3 nego");
			conn->pkt_type = 0x038d;
			cp.max_latency    = cpu_to_le16(0x000a);
			cp.retrans_effort = 0x01;
		} else {
			BT_DBG("hci params for EV3/HV3");
			conn->pkt_type = 0x03c5;
			cp.max_latency    = cpu_to_le16(0xffff);
			cp.retrans_effort = 0xff;
		}

	} else {
		BT_DBG("Error case:");
	}

	BT_DBG("pkt_type modified value = %x", conn->pkt_type);
	cp.pkt_type = cpu_to_le16(conn->pkt_type);

	cp.tx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.rx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.voice_setting  = cpu_to_le16(hdev->voice_setting);

	hci_send_cmd(hdev, HCI_OP_SETUP_SYNC_CONN, sizeof(cp), &cp);
}

static void hci_conn_timeout(unsigned long arg)
{
	struct hci_conn *conn = (void *) arg;
	struct hci_dev *hdev = conn->hdev;
	__u8 reason;

	BT_DBG("conn %p state %d", conn, conn->state);

	if (atomic_read(&conn->refcnt))
		return;

	hci_dev_lock(hdev);

	switch (conn->state) {
	case BT_CONNECT:
	case BT_CONNECT2:
               /* Old Code
		if (conn->type == ACL_LINK && conn->out)
			hci_acl_connect_cancel(conn);
                */
                if (conn->out) {
                        if (conn->type == ACL_LINK)
                                hci_acl_connect_cancel(conn);
                        else if (conn->type == LE_LINK)
                                hci_le_connect_cancel(conn);
                }
		break;
	case BT_CONFIG:
	case BT_CONNECTED:
		reason = hci_proto_disconn_ind(conn);
		hci_acl_disconn(conn, reason);
		break;
	default:
		conn->state = BT_CLOSED;
		break;
	}

	hci_dev_unlock(hdev);
}

static void hci_conn_idle(unsigned long arg)
{
	struct hci_conn *conn = (void *) arg;
	BT_DBG("conn %p mode %d", conn, conn->mode);
	hci_conn_enter_sniff_mode(conn);
}

struct hci_conn *hci_conn_add(struct hci_dev *hdev, int type,
					__u16 pkt_type, bdaddr_t *dst)
{
	struct hci_conn *conn;
	struct hci_conn *tmp_conn;

	BT_DBG("%s dst %s", hdev->name, batostr(dst));

	conn = kzalloc(sizeof(struct hci_conn), GFP_ATOMIC);
	if (!conn)
		return NULL;

	bacpy(&conn->dst, dst);
	conn->hdev  = hdev;
	conn->type  = type;
	conn->mode  = HCI_CM_ACTIVE;
	conn->state = BT_OPEN;
	conn->auth_type = HCI_AT_GENERAL_BONDING;

	conn->power_save = 1;
	tmp_conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, dst);
	if (tmp_conn) {
		memcpy(conn->features, tmp_conn->features, 8);
		BT_DBG("remote features 0x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",
			conn->features[0], conn->features[1],
			conn->features[2], conn->features[3],
			conn->features[4], conn->features[5],
			conn->features[6], conn->features[7]);
	}

	BT_DBG("conn->pkt_type = %x", conn->pkt_type);
	BT_DBG("hdev->esco_type = %x", hdev->esco_type);
	conn->disc_timeout = HCI_DISCONN_TIMEOUT;

	switch (type) {
	case ACL_LINK:
		conn->pkt_type = hdev->pkt_type & ACL_PTYPE_MASK;
		conn->link_policy = hdev->link_policy;
		setup_timer(&conn->idle_timer, hci_conn_idle,
							(unsigned long)conn);
		connectionCount++;
		break;
	case SCO_LINK:
		if (!pkt_type)
			pkt_type = SCO_ESCO_MASK;
	case ESCO_LINK:
		if (!pkt_type)
			pkt_type = ALL_ESCO_MASK;
		if (lmp_esco_capable(hdev)) {
			/* HCI Setup Synchronous Connection Command uses
			   reverse logic on the EDR_ESCO_MASK bits */
			conn->pkt_type = (pkt_type ^ EDR_ESCO_MASK) &
					hdev->esco_type;
		} else {
			/* Legacy HCI Add Sco Connection Command uses a
			   shifted bitmask */
			conn->pkt_type = (pkt_type << 5) & hdev->pkt_type &
					SCO_PTYPE_MASK;
		}
		break;
	}

	BT_DBG("after change conn->pkt_type = %x", conn->pkt_type);
	skb_queue_head_init(&conn->data_q);

	setup_timer(&conn->disc_timer, hci_conn_timeout, (unsigned long)conn);

	atomic_set(&conn->refcnt, 0);

	hci_dev_hold(hdev);

	tasklet_disable(&hdev->tx_task);

	hci_conn_hash_add(hdev, conn);
	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_CONN_ADD);

	atomic_set(&conn->devref, 0);

	hci_conn_init_sysfs(conn);

	tasklet_enable(&hdev->tx_task);

	return conn;
}

int hci_conn_del(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("%s conn %p handle %d", hdev->name, conn, conn->handle);
	del_timer(&conn->disc_timer);

	if (conn->type == ACL_LINK) {
		struct hci_conn *sco = conn->link;
		if (sco)
			sco->link = NULL;

		/* Unacked frames */
		hdev->acl_cnt += conn->sent;
		del_timer(&conn->idle_timer);
		connectionCount--;
		if (!connectionCount)
			releaseWakeLock();
    } else if (conn->type == LE_LINK) {
                if (hdev->le_pkts)
                        hdev->le_cnt += conn->sent;
                else
                        hdev->acl_cnt += conn->sent;
         
	} else {
		struct hci_conn *acl = conn->link;
		if (acl) {
			acl->link = NULL;
			hci_conn_put(acl);
		}
	}

	tasklet_disable(&hdev->tx_task);

	hci_conn_hash_del(hdev, conn);
	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_CONN_DEL);

	tasklet_enable(&hdev->tx_task);

	skb_queue_purge(&conn->data_q);

	hci_conn_put_device(conn);

	hci_dev_put(hdev);

	return 0;
}

struct hci_dev *hci_get_route(bdaddr_t *dst, bdaddr_t *src)
{
	int use_src = bacmp(src, BDADDR_ANY);
	struct hci_dev *hdev = NULL;
	struct list_head *p;

	BT_DBG("%s -> %s", batostr(src), batostr(dst));

	read_lock_bh(&hci_dev_list_lock);

	list_for_each(p, &hci_dev_list) {
		struct hci_dev *d = list_entry(p, struct hci_dev, list);

		if (!test_bit(HCI_UP, &d->flags) || test_bit(HCI_RAW, &d->flags))
			continue;

		/* Simple routing:
		 *   No source address - find interface with bdaddr != dst
		 *   Source address    - find interface with bdaddr == src
		 */

		if (use_src) {
			if (!bacmp(&d->bdaddr, src)) {
				hdev = d; break;
			}
		} else {
			if (bacmp(&d->bdaddr, dst)) {
				hdev = d; break;
			}
		}
	}

	if (hdev)
		hdev = hci_dev_hold(hdev);

	read_unlock_bh(&hci_dev_list_lock);
	return hdev;
}
EXPORT_SYMBOL(hci_get_route);

/* Create SCO or ACL LE connection.
 * Device _must_ be locked */
struct hci_conn *hci_connect(struct hci_dev *hdev, int type,
					__u16 pkt_type, bdaddr_t *dst,
					__u8 sec_level, __u8 auth_type)
{
	struct hci_conn *acl;
	struct hci_conn *sco;
        struct hci_conn *le;

	BT_DBG("%s dst %s", hdev->name, batostr(dst));
         
	if (type == LE_LINK) {
		struct adv_entry *entry;

		le = hci_conn_hash_lookup_ba(hdev, LE_LINK, dst);
		if (!le)
			le = hci_conn_add(hdev, LE_LINK, 0, dst);
		if (!le)
			return NULL;

		entry = hci_find_adv_entry(hdev, dst);
		if (!entry) {
			int err;

			err = hci_do_le_scan(hdev, 3869, 0x00, 0x60, 0x30);
			if (err < 0) {
				hci_conn_del(le);
				return ERR_PTR(err);
			}

			hci_conn_hold(le);
			return le;
		}

		le->dst_type = entry->bdaddr_type;

		hci_le_connect(le);

		hci_conn_hold(le);

		return le;
	}

	if (!(acl = hci_conn_hash_lookup_ba(hdev, ACL_LINK, dst))) {
		if (!(acl = hci_conn_add(hdev, ACL_LINK, 0, dst)))
			return NULL;
	}

	hci_conn_hold(acl);

	if (acl->state == BT_OPEN || acl->state == BT_CLOSED) {
		acl->sec_level = BT_SECURITY_LOW;
		acl->pending_sec_level = sec_level;
		acl->auth_type = auth_type;
		hci_acl_connect(acl);
	}

	if (type == ACL_LINK)
		return acl;

	if (!(sco = hci_conn_hash_lookup_ba(hdev, type, dst))) {
		if (!(sco = hci_conn_add(hdev, type, pkt_type, dst))) {
			hci_conn_put(acl);
			return NULL;
		}
	}

	acl->link = sco;
	sco->link = acl;

	hci_conn_hold(sco);

	if (acl->state == BT_CONNECTED &&
			(sco->state == BT_OPEN || sco->state == BT_CLOSED)) {
		acl->power_save = 1;
		hci_conn_enter_active_mode(acl);

		if (lmp_esco_capable(hdev))
			hci_setup_sync(sco, acl->handle);
		else
			hci_add_sco(sco, acl->handle);
	}

	return sco;
}
EXPORT_SYMBOL(hci_connect);

/* Check link security requirement */
int hci_conn_check_link_mode(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	if (conn->ssp_mode > 0 && conn->hdev->ssp_mode > 0 &&
					!(conn->link_mode & HCI_LM_ENCRYPT))
		return 0;

	return 1;
}
EXPORT_SYMBOL(hci_conn_check_link_mode);

/* Authenticate remote device */
static int hci_conn_auth(struct hci_conn *conn, __u8 sec_level, __u8 auth_type)
{
	BT_DBG("conn %p", conn);

	if (conn->pending_sec_level > sec_level)
		sec_level = conn->pending_sec_level;

	if (sec_level > conn->sec_level)
		conn->pending_sec_level = sec_level;
	else if (conn->link_mode & HCI_LM_AUTH)
		return 1;

	/* Make sure we preserve an existing MITM requirement*/
	auth_type |= (conn->auth_type & 0x01);

	conn->auth_type = auth_type;

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->pend)) {
		struct hci_cp_auth_requested cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(conn->hdev, HCI_OP_AUTH_REQUESTED,
							sizeof(cp), &cp);
	}

	return 0;
}

/* Enable security */
int hci_conn_security(struct hci_conn *conn, __u8 sec_level, __u8 auth_type)
{
	BT_DBG("conn %p", conn);

	if (sec_level == BT_SECURITY_SDP)
		return 1;

	if (sec_level == BT_SECURITY_LOW &&
				(!conn->ssp_mode || !conn->hdev->ssp_mode))
		return 1;

	if (conn->link_mode & HCI_LM_ENCRYPT)
		return hci_conn_auth(conn, sec_level, auth_type);

	if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend))
		return 0;

	if (hci_conn_auth(conn, sec_level, auth_type)) {
		struct hci_cp_set_conn_encrypt cp;
		cp.handle  = cpu_to_le16(conn->handle);
		cp.encrypt = 1;
		hci_send_cmd(conn->hdev, HCI_OP_SET_CONN_ENCRYPT,
							sizeof(cp), &cp);
	}

	return 0;
}
EXPORT_SYMBOL(hci_conn_security);

/* Change link key */
int hci_conn_change_link_key(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->pend)) {
		struct hci_cp_change_conn_link_key cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(conn->hdev, HCI_OP_CHANGE_CONN_LINK_KEY,
							sizeof(cp), &cp);
	}

	return 0;
}
EXPORT_SYMBOL(hci_conn_change_link_key);

/* Switch role */
int hci_conn_switch_role(struct hci_conn *conn, __u8 role)
{
	BT_DBG("conn %p", conn);

	if (!role && conn->link_mode & HCI_LM_MASTER)
		return 1;

	if (!test_and_set_bit(HCI_CONN_RSWITCH_PEND, &conn->pend)) {
		struct hci_cp_switch_role cp;
		bacpy(&cp.bdaddr, &conn->dst);
		cp.role = role;
		hci_send_cmd(conn->hdev, HCI_OP_SWITCH_ROLE, sizeof(cp), &cp);
	}

	return 0;
}
EXPORT_SYMBOL(hci_conn_switch_role);

/* Enter active mode */
void hci_conn_enter_active_mode(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p mode %d", conn, conn->mode);

	if (test_bit(HCI_RAW, &hdev->flags))
		return;

	if (conn->type == (__u8)LE_LINK)
		return;

	if (conn->mode != HCI_CM_SNIFF ||
		(((conn->dev_class[1] & 0x1f) == 0x05) && (!conn->power_save)))
		goto timer;

	if (!test_and_set_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend)) {
		struct hci_cp_exit_sniff_mode cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_EXIT_SNIFF_MODE, sizeof(cp), &cp);
	}

timer:
	if (hdev->idle_timeout > 0)
		mod_timer(&conn->idle_timer,
			jiffies + msecs_to_jiffies(hdev->idle_timeout));

	/*acquire wake lock and start the wake lock timer*/
	startWakeLockTimer();
}

/* Enter sniff mode */
void hci_conn_enter_sniff_mode(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p mode %d", conn, conn->mode);

	if (test_bit(HCI_RAW, &hdev->flags))
		return;

	if (!lmp_sniff_capable(hdev) || !lmp_sniff_capable(conn))
		return;

	if (conn->mode != HCI_CM_ACTIVE || !(conn->link_policy & HCI_LP_SNIFF))
		return;

	if (lmp_sniffsubr_capable(hdev) && lmp_sniffsubr_capable(conn)) {
		struct hci_cp_sniff_subrate cp;
		cp.handle             = cpu_to_le16(conn->handle);
		cp.max_latency        = cpu_to_le16(0);
		cp.min_remote_timeout = cpu_to_le16(0);
		cp.min_local_timeout  = cpu_to_le16(0);
		hci_send_cmd(hdev, HCI_OP_SNIFF_SUBRATE, sizeof(cp), &cp);
	}

	if (!test_and_set_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend)) {
		struct hci_cp_sniff_mode cp;
		cp.handle       = cpu_to_le16(conn->handle);
		cp.max_interval = cpu_to_le16(hdev->sniff_max_interval);
		cp.min_interval = cpu_to_le16(hdev->sniff_min_interval);
		cp.attempt      = cpu_to_le16(4);
		cp.timeout      = cpu_to_le16(1);
		hci_send_cmd(hdev, HCI_OP_SNIFF_MODE, sizeof(cp), &cp);
	}
}

/* Drop all connection on the device */
void hci_conn_hash_flush(struct hci_dev *hdev)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct list_head *p;

	BT_DBG("hdev %s", hdev->name);

	p = h->list.next;
	while (p != &h->list) {
		struct hci_conn *c;

		c = list_entry(p, struct hci_conn, list);
		p = p->next;

		c->state = BT_CLOSED;

		hci_proto_disconn_cfm(c, 0x16);
		hci_conn_del(c);
	}
}

/* Check pending connect attempts */
void hci_conn_check_pending(struct hci_dev *hdev, u8 type)
{
	struct hci_conn *conn;
	struct adv_entry *entry;

	BT_DBG("hdev %s", hdev->name);

	hci_dev_lock(hdev);

	switch (type) {
	case ACL_LINK:
		conn = hci_conn_hash_lookup_state(hdev, ACL_LINK, BT_CONNECT2);
		if (conn)
			hci_acl_connect(conn);

		break;
	case LE_LINK:
		conn = hci_conn_hash_lookup_state(hdev, LE_LINK, BT_OPEN);
		if (!conn)
			goto unlock;

		entry = hci_find_adv_entry(hdev, &conn->dst);
		if (!entry) {
			u8 status = 0x04; /* mapped into EHOSTDOWN errno */
			hci_proto_connect_cfm(conn, status);
			conn->state = BT_CLOSED;
			hci_conn_del(conn);
			goto unlock;
		}

		conn->dst_type = entry->bdaddr_type;
		hci_le_connect(conn);

		break;
	}

unlock:
	hci_dev_unlock(hdev);
}

void hci_conn_hold_device(struct hci_conn *conn)
{
	atomic_inc(&conn->devref);
}
EXPORT_SYMBOL(hci_conn_hold_device);

void hci_conn_put_device(struct hci_conn *conn)
{
	if (atomic_dec_and_test(&conn->devref))
		hci_conn_del_sysfs(conn);
}
EXPORT_SYMBOL(hci_conn_put_device);

int hci_get_conn_list(void __user *arg)
{
	struct hci_conn_list_req req, *cl;
	struct hci_conn_info *ci;
	struct hci_dev *hdev;
	struct list_head *p;
	int n = 0, size, err;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	if (!req.conn_num || req.conn_num > (PAGE_SIZE * 2) / sizeof(*ci))
		return -EINVAL;

	size = sizeof(req) + req.conn_num * sizeof(*ci);

	if (!(cl = kmalloc(size, GFP_KERNEL)))
		return -ENOMEM;

	if (!(hdev = hci_dev_get(req.dev_id))) {
		kfree(cl);
		return -ENODEV;
	}

	ci = cl->conn_info;

	hci_dev_lock_bh(hdev);
	list_for_each(p, &hdev->conn_hash.list) {
		register struct hci_conn *c;
		c = list_entry(p, struct hci_conn, list);

		bacpy(&(ci + n)->bdaddr, &c->dst);
		(ci + n)->handle = c->handle;
		(ci + n)->type  = c->type;
		(ci + n)->out   = c->out;
		(ci + n)->state = c->state;
		(ci + n)->link_mode = c->link_mode;
		if (c->type == SCO_LINK) {
			(ci + n)->mtu = hdev->sco_mtu;
			(ci + n)->cnt = hdev->sco_cnt;
			(ci + n)->pkts = hdev->sco_pkts;
		} else {
			(ci + n)->mtu = hdev->acl_mtu;
			(ci + n)->cnt = hdev->acl_cnt;
			(ci + n)->pkts = hdev->acl_pkts;
		}
		if (++n >= req.conn_num)
			break;
	}
	hci_dev_unlock_bh(hdev);

	cl->dev_id = hdev->id;
	cl->conn_num = n;
	size = sizeof(req) + n * sizeof(*ci);

	hci_dev_put(hdev);

	err = copy_to_user(arg, cl, size);
	kfree(cl);

	return err ? -EFAULT : 0;
}

int hci_get_conn_info(struct hci_dev *hdev, void __user *arg)
{
	struct hci_conn_info_req req;
	struct hci_conn_info ci;
	struct hci_conn *conn;
	char __user *ptr = arg + sizeof(req);

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	hci_dev_lock_bh(hdev);
	conn = hci_conn_hash_lookup_ba(hdev, req.type, &req.bdaddr);
	if (conn) {
		bacpy(&ci.bdaddr, &conn->dst);
		ci.handle = conn->handle;
		ci.type  = conn->type;
		ci.out   = conn->out;
		ci.state = conn->state;
		ci.link_mode = conn->link_mode;
		if (req.type == SCO_LINK) {
			ci.mtu = hdev->sco_mtu;
			ci.cnt = hdev->sco_cnt;
			ci.pkts = hdev->sco_pkts;
		} else {
			ci.mtu = hdev->acl_mtu;
			ci.cnt = hdev->acl_cnt;
			ci.pkts = hdev->acl_pkts;
		}
	}
	hci_dev_unlock_bh(hdev);

	if (!conn)
		return -ENOENT;

	return copy_to_user(ptr, &ci, sizeof(ci)) ? -EFAULT : 0;
}

int hci_get_auth_info(struct hci_dev *hdev, void __user *arg)
{
	struct hci_auth_info_req req;
	struct hci_conn *conn;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	hci_dev_lock_bh(hdev);
	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &req.bdaddr);
	if (conn)
		req.type = conn->auth_type;
	hci_dev_unlock_bh(hdev);

	if (!conn)
		return -ENOENT;

	return copy_to_user(arg, &req, sizeof(req)) ? -EFAULT : 0;
}

void waklock_timeout_callback(unsigned long val)
{
	releaseWakeLock();
}
static void startWakeLockTimer(void)
{
	if (!lockFlag) {
		wake_lock(&idletimerlock);
		lockFlag = 1;
	}
	/*start or re start wake lock timer as well*/
	mod_timer(&wakeLockTimer,
			jiffies + msecs_to_jiffies(HCI_WAKE_LOCK_TIMER));

}
static void releaseWakeLock(void)
{
	if (lockFlag) {
		wake_unlock(&idletimerlock);
		lockFlag = 0;
	}
}