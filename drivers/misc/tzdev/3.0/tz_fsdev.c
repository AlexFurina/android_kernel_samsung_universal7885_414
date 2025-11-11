/*
 * Copyright (C) 2017 Samsung Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include "lib/circ_buf.h"
#include "lib/circ_buf_packet.h"

#include "tzdev.h"
#include "sysdep.h"
#include "tz_cdev.h"
#include "tz_fsdev.h"
#include "tz_iwio.h"
#include "tz_iwsock.h"
#include "tz_mem.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aleksandr Aleksandrov");
MODULE_DESCRIPTION("Trustzone file system driver");

#define LOG_KTHREAD	"[TZ_FSDEV_TH] "
#define NWFS_CMD_SOCK "socket://nwfs_cmd_sock"
#define SESSION_BUF_PAGES 5
#define MAX_SESSION_DATA_SIZE (SESSION_BUF_PAGES * PAGE_SIZE - sizeof(uint32_t))

/* fsdev <--> SK NWFS driver */
struct tz_fsdev_session_buf {
	uint32_t packet_size;
	char packet[];
} __packet;

struct tz_fsdev_ubuf_pin_info {
	struct page **pages;
	unsigned long nr_pages;
	struct mm_struct *mm;
	sk_pfn_t *pfns;
};

struct tz_fsdev_session_ctx {
	unsigned int session_id;
	pid_t pid;
	struct tz_fsdev_request_ctx *request_in_progress;
	struct list_head cmd_list;
	struct completion c;
	struct completion c_init;
	struct list_head link;
	spinlock_t lock;
	struct tz_fsdev_session_buf *session_buf;
	struct tz_fsdev_ubuf_pin_info pin_info;
};

struct tz_fsdev_request_from_swd {
	uint32_t session_id;
	uint32_t cmd;
} __packed;

struct tz_fsdev_request_ctx {
	struct tz_fsdev_data command;
	struct list_head link;
};

static LIST_HEAD(tz_fsdev_session_ctx_list);
static LIST_HEAD(tz_fsdev_new_session_ctx_list);

static struct task_struct *task;
static DEFINE_SPINLOCK(session_ctx_lock);
static struct tz_fsdev_session_ctx *nwfs_daemon_ctx;
static struct sock_desc *tz_fsdev_contrl_sock;

static int tz_fsdev_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int tz_fsdev_pre_connect_callback(void *buf,
		unsigned long num_pages, void *ext_data)
{
	*(uint32_t *)buf = *(uint32_t *)ext_data;

	return 0;
}

static int tz_fsdev_free_session_buf(struct tz_fsdev_session_ctx *session_ctx)
{
	tz_iwio_free_iw_channel(session_ctx->session_buf);
	session_ctx->session_buf = NULL;

	return 0;
}

static struct tz_fsdev_session_ctx *create_session_ctx(void)
{
	struct tz_fsdev_session_ctx *session_ctx;

	session_ctx = kmalloc(sizeof(struct tz_fsdev_session_ctx), GFP_KERNEL);
	if (!session_ctx)
		return NULL;

	memset(session_ctx, 0, sizeof(struct tz_fsdev_session_ctx));

	INIT_LIST_HEAD(&session_ctx->cmd_list);
	spin_lock_init(&session_ctx->lock);
	init_completion(&session_ctx->c);
	init_completion(&session_ctx->c_init);

	return session_ctx;
}

static void release_session_ctx(struct tz_fsdev_session_ctx *session_ctx)
{
	spin_lock(&session_ctx_lock);
	list_del(&session_ctx->link);
	spin_unlock(&session_ctx_lock);

	kfree(session_ctx);
}

static struct tz_fsdev_session_ctx *__tz_fsdev_get_session_ctx_by_pid(pid_t pid)
{
	struct tz_fsdev_session_ctx *session_ctx, *tmp;

	if (nwfs_daemon_ctx->pid == pid)
		return nwfs_daemon_ctx;

	list_for_each_entry_safe(session_ctx, tmp, &tz_fsdev_session_ctx_list, link)
		if (session_ctx->pid == pid)
			return session_ctx;

	return NULL;
}

static struct tz_fsdev_session_ctx *tz_fsdev_get_session_ctx_by_pid(pid_t pid)
{
	struct tz_fsdev_session_ctx *session_ctx;

	spin_lock(&session_ctx_lock);
	session_ctx = __tz_fsdev_get_session_ctx_by_pid(pid);
	spin_unlock(&session_ctx_lock);

	return session_ctx;
}

static int tz_fsdev_get_command(pid_t pid, struct tz_fsdev_data *cmd_data)
{
	struct tz_fsdev_session_ctx *session_ctx;
	int ret;
	struct tz_fsdev_request_ctx *request;

	session_ctx = tz_fsdev_get_session_ctx_by_pid(pid);
	if (!session_ctx)
		return -EPERM;

	ret = wait_for_completion_interruptible(&session_ctx->c);
	if (ret < 0)
		return ret;

	spin_lock(&session_ctx->lock);
	request = list_first_entry(&session_ctx->cmd_list, struct tz_fsdev_request_ctx, link);

	*cmd_data = request->command;

	session_ctx->request_in_progress = request;
	list_del(&request->link);
	spin_unlock(&session_ctx->lock);

	return 0;
}

static int tz_fsdev_get_wp_command(pid_t pid, struct tz_fsdev_wp_command *wp_command)
{
	struct tz_fsdev_session_ctx *session_ctx;
	int ret;
	struct tz_fsdev_request_ctx *request;

	session_ctx = tz_fsdev_get_session_ctx_by_pid(pid);
	if (!session_ctx)
		return -EPERM;

	ret = wait_for_completion_interruptible(&session_ctx->c);
	if (ret < 0)
		return ret;

	spin_lock(&session_ctx->lock);
	request = list_first_entry(&session_ctx->cmd_list, struct tz_fsdev_request_ctx, link);

	session_ctx->request_in_progress = request;
	list_del(&request->link);
	spin_unlock(&session_ctx->lock);

	wp_command->cmd = request->command.cmd;
	wp_command->size = session_ctx->session_buf->packet_size;

	if (session_ctx->session_buf->packet_size > 0)
		if (copy_to_user((void __user *)wp_command->buf, session_ctx->session_buf->packet, session_ctx->session_buf->packet_size))
			return -EFAULT;

	return 0;
}

static int __tz_fsdev_send_responce_to_swd(unsigned int session_id, int status)
{
	ssize_t ret_cnt;
	ssize_t data_cnt = sizeof(struct tz_fsdev_request_from_swd);
	struct tz_fsdev_request_from_swd iw_cmd_data = {
		.session_id = session_id,
		.cmd = status,
	};

	ret_cnt = tz_iwsock_write(tz_fsdev_contrl_sock, (void *)&iw_cmd_data, data_cnt, 0);
	if (ret_cnt != data_cnt)
		return -1;

	return 0;
}

static int tz_fsdev_send_responce_to_swd(struct tz_fsdev_session_ctx *session_ctx, int status)
{
	if (!session_ctx->request_in_progress)
		return -EINVAL;

	return __tz_fsdev_send_responce_to_swd(session_ctx->session_id, status);
}

static struct tz_fsdev_session_ctx *__tz_fsdev_get_new_session_ctx_by_pid(pid_t pid)
{
	struct tz_fsdev_session_ctx *session_ctx, *tmp;

	list_for_each_entry_safe(session_ctx, tmp, &tz_fsdev_new_session_ctx_list, link)
		if (session_ctx->pid == pid)
			return session_ctx;

	return NULL;
}

static struct tz_fsdev_session_ctx *__tz_fsdev_get_new_session_ctx_by_session_id(unsigned int session_id)
{
	struct tz_fsdev_session_ctx *session_ctx, *tmp;

	list_for_each_entry_safe(session_ctx, tmp, &tz_fsdev_new_session_ctx_list, link)
		if (session_ctx->session_id == session_id)
			return session_ctx;

	return NULL;
}

static struct tz_fsdev_session_ctx *tz_fsdev_get_new_session_ctx_by_session_id(unsigned int session_id)
{
	struct tz_fsdev_session_ctx *session_ctx;

	spin_lock(&session_ctx_lock);
	session_ctx = __tz_fsdev_get_new_session_ctx_by_session_id(session_id);
	spin_unlock(&session_ctx_lock);

	return session_ctx;
}

static int tz_fsdev_free_new_session_ctx_and_responce(struct tz_fsdev_session_ctx *session_ctx, int err_code)
{
	int ret;

	ret = tz_fsdev_send_responce_to_swd(session_ctx, err_code);

	kfree(session_ctx->request_in_progress);
	release_session_ctx(session_ctx);

	return ret;
}

static struct tz_fsdev_session_ctx *__tz_fsdev_search_new_session_ctx(int cur_pid)
{
	struct tz_fsdev_session_ctx *session_ctx;

	session_ctx = __tz_fsdev_get_new_session_ctx_by_pid(cur_pid);
	if (!session_ctx)
		return __tz_fsdev_get_new_session_ctx_by_session_id(nwfs_daemon_ctx->request_in_progress->command.session_id);

	return session_ctx;
}

static void tz_fsdev_free_pfns(struct tz_fsdev_ubuf_pin_info *pin_info)
{
	if (pin_info->pfns) {
#if defined(CONFIG_TZDEV_PAGE_MIGRATION)
		tzdev_put_user_pages(pin_info->pages, pin_info->nr_pages);
		tzdev_decrease_pinned_vm(pin_info->mm, pin_info->nr_pages);
#endif /* CONFIG_TZDEV_PAGE_MIGRATION */
		mmput(pin_info->mm);
		kfree(pin_info->pfns);
		kfree(pin_info->pages);
	}
}

static sk_pfn_t *tz_fsdev_get_pfns_and_pin(void *buf, unsigned int size, struct tz_fsdev_ubuf_pin_info *pin_info)
{
	int ret;
	struct mm_struct *mm;
	struct task_struct *task;
	struct page **pages;
	sk_pfn_t *pfns;
	uint32_t pfns_size;
	unsigned long nr_pages;
	unsigned int i;

	nr_pages = DIV_ROUND_UP(size, PAGE_SIZE);
	if (!nr_pages)
		return ERR_PTR(-ENOMEM);

	pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	pfns_size = nr_pages * sizeof(sk_pfn_t);
	pfns = kmalloc(pfns_size, GFP_KERNEL);
	if (!pfns) {
		ret = -ENOMEM;
		goto out_pages;
	}

	task = current;
	mm = get_task_mm(task);
	if (!mm) {
		ret = -ESRCH;
		goto out_pfns;
	}

	ret = tzdev_get_user_pages(task, mm, (unsigned long __user)buf,
			nr_pages, 1, 0, pages, NULL);
	if (ret) {
		tzdev_print(0, "Failed to pin user pages (%d)\n", ret);
		goto out_mm;
	}

#if defined(CONFIG_TZDEV_PAGE_MIGRATION)
		/*
		 * In case of enabled migration it is possible that userspace pages
		 * will be migrated from current physical page to some other
		 * To avoid fails of CMA migrations we have to move pages to other
		 * region which can not be inside any CMA region. This is done by
		 * allocations with GFP_KERNEL flag to point UNMOVABLE memblock
		 * to be used for such allocations.
		 */
		ret = tzdev_migrate_pages(task, mm, (unsigned long __user)buf, nr_pages,
				1, 0, pages);
		if (ret < 0) {
			tzdev_print(0, "Failed to migrate CMA pages (%d)\n", ret);
			goto out_pin;
		}
#endif /* CONFIG_TZDEV_PAGE_MIGRATION */

	for (i = 0; i < nr_pages; i++)
		pfns[i] = page_to_pfn(pages[i]);

	pin_info->pages = pages;
	pin_info->nr_pages = nr_pages;
	pin_info->mm = mm;
	pin_info->pfns = pfns;

	return pfns;

#if defined(CONFIG_TZDEV_PAGE_MIGRATION)
out_pin:
	tzdev_put_user_pages(pages, nr_pages);
	tzdev_decrease_pinned_vm(mm, nr_pages);
#endif /* CONFIG_TZDEV_PAGE_MIGRATION */
out_mm:
	mmput(mm);
out_pfns:
	kfree(pfns);
out_pages:
	kfree(pages);

	pin_info->pages = NULL;
	pin_info->nr_pages = 0;
	pin_info->mm = NULL;
	pin_info->pfns = NULL;

	return ERR_PTR(ret);
}

static int tz_fsdev_try_to_release_ctx(pid_t cur_pid)
{
	struct tz_fsdev_session_ctx *session_ctx;
	int ret;
	unsigned int session_id;
	unsigned int cmd;

	spin_lock(&session_ctx_lock);
	session_ctx = __tz_fsdev_get_session_ctx_by_pid(cur_pid);
	if (!session_ctx) {
		session_ctx = __tz_fsdev_search_new_session_ctx(cur_pid);
		if (!session_ctx) {
			spin_unlock(&session_ctx_lock);
			tzdev_print(0, "Session ctx is not found\n");

			return -ESRCH;
		}
		list_del(&session_ctx->link);
		spin_unlock(&session_ctx_lock);

		if (session_ctx->request_in_progress) {
			session_id = session_ctx->session_id;

			kfree(session_ctx->request_in_progress);
			ret = __tz_fsdev_send_responce_to_swd(session_id, -EFAULT);
		} else {
			ret = -EFAULT;
		}

		kfree(session_ctx);

		return ret;
	}

	session_ctx->pid = -1;
	spin_unlock(&session_ctx_lock);
	tz_fsdev_free_pfns(&session_ctx->pin_info);
	session_ctx->pin_info.pfns = NULL;

	if (session_ctx->request_in_progress) {
		cmd = session_ctx->request_in_progress->command.cmd;
		session_id = session_ctx->session_id;

		kfree(session_ctx->request_in_progress);
		session_ctx->request_in_progress = NULL;

		if (cmd == NWFS_CMD_CLOSE_SESSION) {
			ret = 0;
			release_session_ctx(session_ctx);
		} else {
			ret = -EIO;
		}

		ret = __tz_fsdev_send_responce_to_swd(session_id, ret);
	} else {
		ret = -EFAULT;
	}

	return ret;
}

static int tz_fsdev_reply(struct tz_fsdev_reply *reply)
{
	struct tz_fsdev_session_ctx *session_ctx;
	unsigned int session_id;
	int ret_swd;

	session_ctx = tz_fsdev_get_new_session_ctx_by_session_id(reply->session_id);
	if (!session_ctx || !nwfs_daemon_ctx->request_in_progress)
		return -ESRCH;

	session_id = nwfs_daemon_ctx->request_in_progress->command.session_id;
	if (session_id != session_ctx->session_id)
		return -EPERM;

	nwfs_daemon_ctx->request_in_progress = NULL;

	if (reply->pid < 0) {
		kfree(session_ctx->request_in_progress);
		ret_swd = __tz_fsdev_send_responce_to_swd(session_id, -EFAULT);

		kfree(session_ctx);
		return ret_swd;
	}

	session_ctx->pid = reply->pid;
	complete(&session_ctx->c_init);

	return 0;
}

static int tz_fsdev_reply_wp_command(pid_t pid, struct tz_fsdev_wp_reply *wp_reply)
{
	struct tz_fsdev_session_ctx *session_ctx;
	uint32_t size;
	uint32_t ret_val;
	void *ubuf;
	int ret;
	sk_pfn_t *pfns;
	int max_pfn_count;
	int total_pfn_count;
	int pfn_count;
	int i;
	struct tz_fsdev_request_ctx *request;

	session_ctx = tz_fsdev_get_session_ctx_by_pid(pid);
	if (!session_ctx)
		return -ENOENT;

	ret_val = wp_reply->ret;
	size = wp_reply->size;
	ubuf = (void *)wp_reply->buf;

	if (!size) {
		session_ctx->session_buf->packet_size = 0;
		ret = tz_fsdev_send_responce_to_swd(session_ctx, ret_val);
		kfree(session_ctx->request_in_progress);
		session_ctx->request_in_progress = NULL;

		return ret;
	}

	if (session_ctx->pin_info.pfns) {
		if (wp_reply->is_new_buf) {
			tz_fsdev_free_pfns(&session_ctx->pin_info);
			pfns = tz_fsdev_get_pfns_and_pin(ubuf, size, &session_ctx->pin_info);
		} else {
			pfns = session_ctx->pin_info.pfns;
		}
	} else {
		pfns = tz_fsdev_get_pfns_and_pin(ubuf, size, &session_ctx->pin_info);
	}

	if (PTR_RET(pfns)) {
		session_ctx->session_buf->packet_size = 0;
		ret_val = -ENOMEM;

		ret = tz_fsdev_send_responce_to_swd(session_ctx, ret_val);
		kfree(session_ctx->request_in_progress);
		session_ctx->request_in_progress = NULL;
		if (ret)
			return ret;
		return ret_val;
	}

	total_pfn_count = DIV_ROUND_UP(size, PAGE_SIZE);
	max_pfn_count = MAX_SESSION_DATA_SIZE / sizeof(sk_pfn_t);

	if (total_pfn_count < max_pfn_count) {
		session_ctx->session_buf->packet_size = sizeof(sk_pfn_t) * total_pfn_count;
		memcpy(session_ctx->session_buf->packet, pfns, session_ctx->session_buf->packet_size);
		ret = tz_fsdev_send_responce_to_swd(session_ctx, ret_val);
		kfree(session_ctx->request_in_progress);
		session_ctx->request_in_progress = NULL;

		return ret;
	}

	i = 0;
	pfn_count = max_pfn_count;
	while (i < total_pfn_count) {
		session_ctx->session_buf->packet_size = sizeof(sk_pfn_t) * pfn_count;
		memcpy(session_ctx->session_buf->packet, &pfns[i], session_ctx->session_buf->packet_size);

		i += pfn_count;
		ret = tz_fsdev_send_responce_to_swd(session_ctx, ret_val);
		kfree(session_ctx->request_in_progress);
		session_ctx->request_in_progress = NULL;
		if (i >= total_pfn_count || ret)
			return ret;

		ret = wait_for_completion_interruptible(&session_ctx->c);
		if (ret < 0)
			return ret;

		spin_lock(&session_ctx->lock);
		request = list_first_entry(&session_ctx->cmd_list, struct tz_fsdev_request_ctx, link);

		session_ctx->request_in_progress = request;
		list_del(&request->link);
		spin_unlock(&session_ctx->lock);

		if (request->command.cmd != NWFS_CMD_READ_FILE_CONTINUE) {
			session_ctx->session_buf->packet_size = 0;
			ret_val = -ECOMM;

			ret = tz_fsdev_send_responce_to_swd(session_ctx, ret_val);
			kfree(session_ctx->request_in_progress);
			session_ctx->request_in_progress = NULL;
			break;
		}

		if (total_pfn_count - i > max_pfn_count)
			pfn_count = max_pfn_count;
		else
			pfn_count = total_pfn_count - i;
	}

	return ret;
}

static int tz_fsdev_create_session_buffer(pid_t cur_pid, unsigned int session_id)
{
	struct tz_fsdev_session_ctx *session_ctx;
	int ret;
	int ret_swd;

	session_ctx = tz_fsdev_get_new_session_ctx_by_session_id(session_id);
	if (!session_ctx)
		return -ENOENT;

	ret = wait_for_completion_interruptible(&session_ctx->c_init);
	if (ret < 0)
		return ret;

	if (session_ctx->pid != cur_pid)
		return -EPERM;

	if (session_ctx->session_buf)
		return -EINVAL;

	session_ctx->session_buf = tz_iwio_alloc_iw_channel(TZ_IWIO_CONNECT_NWFS,
				SESSION_BUF_PAGES, tz_fsdev_pre_connect_callback,
				NULL, &session_ctx->session_id);
	if (IS_ERR_OR_NULL(session_ctx->session_buf)) {
		ret = PTR_ERR(session_ctx->session_buf);
		session_ctx->session_buf = NULL;
		goto out_err;
	}

	spin_lock(&session_ctx_lock);
	list_del(&session_ctx->link);
	list_add_tail(&session_ctx->link, &tz_fsdev_session_ctx_list);
	spin_unlock(&session_ctx_lock);

	kfree(session_ctx->request_in_progress);
	session_ctx->request_in_progress = NULL;

	ret = __tz_fsdev_send_responce_to_swd(session_id, 0);
	if (ret)
		goto out_err;

	return 0;

out_err:
	tzdev_print(0, "Create session buffer ERROR. ret = %d\n", ret);
	ret_swd = tz_fsdev_free_new_session_ctx_and_responce(session_ctx, ret);
	if (ret_swd)
		return ret_swd;

	return ret;
}

static long tz_fsdev_common_ioctl(pid_t cur_pid, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct tz_fsdev_session_ctx *session_ctx;

	switch (cmd) {
	case TZ_FSDEV_REG_DAEMON: {
		if (nwfs_daemon_ctx->pid < 0) {
			nwfs_daemon_ctx->pid = cur_pid;

			return 0;
		}

		return -EPERM;
	}
	case TZ_FSDEV_GET_CMD: {
		struct tz_fsdev_data __user *argp = (struct tz_fsdev_data __user *)arg;
		struct tz_fsdev_data cmd_data;

		ret = tz_fsdev_get_command(cur_pid, &cmd_data);
		if (ret)
			return ret;

		if (copy_to_user(argp, &cmd_data, sizeof(struct tz_fsdev_data)))
			return -EFAULT;

		return 0;
	}
	case TZ_FSDEV_REPLY: {
		struct tz_fsdev_reply __user *argp = (struct tz_fsdev_reply __user *)arg;
		struct tz_fsdev_reply reply;

		if (cur_pid != nwfs_daemon_ctx->pid)
			return -EPERM;

		if (copy_from_user(&reply, argp, sizeof(struct tz_fsdev_reply)))
			return -EFAULT;

		return tz_fsdev_reply(&reply);
	}
	case TZ_FSDEV_CREATE_SESSION_BUF: {
		unsigned int session_id = (unsigned int)arg;

		return tz_fsdev_create_session_buffer(cur_pid, session_id);
	}
	case TZ_FSDEV_FREE_SESSION_BUF: {
		session_ctx = tz_fsdev_get_session_ctx_by_pid(cur_pid);
		if (!session_ctx)
			return -ENOENT;

		return tz_fsdev_free_session_buf(session_ctx);
	}
	case TZ_FSDEV_UNEXPECTED_EXIT: {
		struct tz_fsdev_child_exit __user *argp = (struct tz_fsdev_child_exit __user *)arg;
		struct tz_fsdev_child_exit child_exit;
		int ret;

		if (cur_pid != nwfs_daemon_ctx->pid)
			return -EPERM;

		if (copy_from_user(&child_exit, argp, sizeof(struct tz_fsdev_child_exit)))
			return -EFAULT;

		tzdev_print(0, "WP (pid %d) finished with signal %d\n", child_exit.pid, child_exit.status);

		ret = tz_fsdev_try_to_release_ctx(child_exit.pid);
		if (!ret || ret == -ESRCH)
			return 0;

		return ret;
	}
	default:
		tzdev_print(0, "Unexpected command %d from %d pid\n", cmd, cur_pid);
		return -ENOTTY;
	}

	return 0;
}

static long tz_fsdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	pid_t cur_pid = current->pid;

	switch (cmd) {
	case TZ_FSDEV_GET_WP_CMD: {
		struct tz_fsdev_wp_command __user *argp = (struct tz_fsdev_wp_command __user *)arg;
		struct tz_fsdev_wp_command wp_command;

		if (copy_from_user(&wp_command, argp, sizeof(struct tz_fsdev_wp_command)))
			return -EFAULT;

		ret = tz_fsdev_get_wp_command(cur_pid, &wp_command);

		if (copy_to_user(argp, &wp_command, sizeof(struct tz_fsdev_wp_command)))
			return -EFAULT;

		return ret;
	}
	case TZ_FSDEV_REPLY_WP_CMD: {
		struct tz_fsdev_wp_reply __user *argp = (struct tz_fsdev_wp_reply __user *)arg;
		struct tz_fsdev_wp_reply wp_reply;

		if (copy_from_user(&wp_reply, argp, sizeof(struct tz_fsdev_wp_reply)))
			return -EFAULT;

		return tz_fsdev_reply_wp_command(cur_pid, &wp_reply);
	}
	default:
		return tz_fsdev_common_ioctl(cur_pid, cmd, arg);
	}

	return 0;
}

#ifdef CONFIG_COMPAT
static long compat_tz_fsdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	pid_t cur_pid = current->pid;

	switch (cmd) {
	case COMPAT_TZ_FSDEV_GET_WP_CMD: {
		struct compat_tz_fsdev_wp_command __user *argp = (struct compat_tz_fsdev_wp_command __user *)arg;
		struct compat_tz_fsdev_wp_command compat_wp_command;
		struct tz_fsdev_wp_command wp_command = {0};

		if (copy_from_user(&compat_wp_command, argp, sizeof(struct compat_tz_fsdev_wp_command)))
			return -EFAULT;

		wp_command.buf = compat_ptr(compat_wp_command.buf);

		ret = tz_fsdev_get_wp_command(cur_pid, &wp_command);

		compat_wp_command.cmd = wp_command.cmd;
		compat_wp_command.size = wp_command.size;

		if (copy_to_user(argp, &compat_wp_command, sizeof(struct compat_tz_fsdev_wp_command)))
			return -EFAULT;

		return ret;
	}
	case COMPAT_TZ_FSDEV_REPLY_WP_CMD: {
		struct compat_tz_fsdev_wp_reply __user *argp = (struct compat_tz_fsdev_wp_reply __user *)arg;
		struct compat_tz_fsdev_wp_reply compat_wp_reply;
		struct tz_fsdev_wp_reply wp_reply;

		if (copy_from_user(&compat_wp_reply, argp, sizeof(struct compat_tz_fsdev_wp_reply)))
			return -EFAULT;

		wp_reply.ret = compat_wp_reply.ret;
		wp_reply.size = compat_wp_reply.size;
		wp_reply.is_new_buf = compat_wp_reply.is_new_buf;
		wp_reply.buf = compat_ptr(compat_wp_reply.buf);

		return tz_fsdev_reply_wp_command(cur_pid, &wp_reply);
	}
	default:
		return tz_fsdev_common_ioctl(cur_pid, cmd, arg);
	}

	return 0;
}
#endif /* CONFIG_COMPAT */

static int tz_fsdev_release(struct inode *inode, struct file *filp)
{
	return tz_fsdev_try_to_release_ctx(current->pid);
}

static const struct file_operations tz_fsdev_fops = {
	.owner = THIS_MODULE,
	.open = tz_fsdev_open,
	.unlocked_ioctl = tz_fsdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_tz_fsdev_ioctl,
#endif /* CONFIG_COMPAT */
	.release = tz_fsdev_release,
};

static struct tz_cdev tz_fsdev_cdev = {
	.name = TZ_FSDEV_NAME,
	.fops = &tz_fsdev_fops,
	.owner = THIS_MODULE,
};

static struct tz_fsdev_session_ctx *__tz_fsdev_get_session_ctx_by_session_id(unsigned int session_id)
{
	struct tz_fsdev_session_ctx *session_ctx;

	list_for_each_entry(session_ctx, &tz_fsdev_session_ctx_list, link)
		if (session_ctx->session_id == session_id)
			return session_ctx;

	return NULL;
}

static int __tz_fsdev_is_unique_session_id(unsigned int session_id)
{
	struct tz_fsdev_session_ctx *session_ctx;

	list_for_each_entry(session_ctx, &tz_fsdev_session_ctx_list, link)
		if (session_ctx->session_id == session_id)
			return 0;

	list_for_each_entry(session_ctx, &tz_fsdev_new_session_ctx_list, link)
		if (session_ctx->session_id == session_id)
			return 0;

	return 1;
}

static int tz_fsdev_put_command(unsigned int cmd, unsigned int session_id)
{
	struct tz_fsdev_session_ctx *session_ctx;
	struct tz_fsdev_session_ctx *new_session_ctx;
	struct tz_fsdev_request_ctx *request;
	int ret;
	int ret_swd;

	request = kmalloc(sizeof(struct tz_fsdev_request_ctx), GFP_KERNEL);
	if (!request) {
		ret = -ENOMEM;
		goto finish_command;
	}

	request->command.cmd = cmd;
	request->command.session_id = session_id;

	if (cmd == NWFS_CMD_CREATE_SESSION) {
		new_session_ctx = create_session_ctx();
		if (!new_session_ctx) {
			ret = -EFAULT;
			goto finish_command;
		}

		spin_lock(&session_ctx_lock);
		if (!__tz_fsdev_is_unique_session_id(session_id)) {
			spin_unlock(&session_ctx_lock);
			kfree(new_session_ctx);
			ret = -EINVAL;
			goto finish_command;
		}

		new_session_ctx->session_id = session_id;
		new_session_ctx->request_in_progress = request;

		list_add(&new_session_ctx->link, &tz_fsdev_new_session_ctx_list);
		spin_unlock(&session_ctx_lock);

		session_ctx = nwfs_daemon_ctx;
	} else {
		spin_lock(&session_ctx_lock);
		session_ctx = __tz_fsdev_get_session_ctx_by_session_id(session_id);
		spin_unlock(&session_ctx_lock);

		if (!session_ctx || cmd >= NWFS_CMD_CNT) {
			tzdev_print(0, "Unknown params. session_id = %d, cmd = %d\n", session_id, cmd);
			ret = -EINVAL;
			goto finish_command;
		}

		if (session_ctx->pid < 0) {
			if (cmd == NWFS_CMD_CLOSE_SESSION) {
				tz_fsdev_free_session_buf(session_ctx);
				release_session_ctx(session_ctx);
				ret = 0;
			} else {
				ret = -EBADSLT;
			}

			goto finish_command;
		}
	}

	spin_lock(&session_ctx->lock);
	list_add_tail(&request->link, &session_ctx->cmd_list);
	complete(&session_ctx->c);
	spin_unlock(&session_ctx->lock);

	return 0;

finish_command:
	if (request)
		kfree(request);

	ret_swd = __tz_fsdev_send_responce_to_swd(session_id, ret);
	if (ret_swd)
		return ret_swd;

	return ret;
}

static int tz_fsdev_thread_function(void *data)
{
	struct tz_fsdev_request_from_swd iw_cmd_data;
	struct sock_desc *listen_sock;
	int ret;
	ssize_t read_cnt;

	listen_sock = tz_iwsock_socket(1);
	if (IS_ERR_OR_NULL(listen_sock))
		return -1;

	ret = tz_iwsock_listen(listen_sock, NWFS_CMD_SOCK);
	if (ret)
		return -1;

	tz_fsdev_contrl_sock = tz_iwsock_accept(listen_sock);
	if (IS_ERR_OR_NULL(tz_fsdev_contrl_sock)) {
		tzdev_print(0, LOG_KTHREAD "Failed to accept, error %ld\n", PTR_ERR(tz_fsdev_contrl_sock));
		return -1;
	}

	while (1) {
		read_cnt = tz_iwsock_read(tz_fsdev_contrl_sock, (void *)&iw_cmd_data,
				sizeof(struct tz_fsdev_request_from_swd), 0);
		if (read_cnt == 0) {
			tzdev_print(0, LOG_KTHREAD "NWFS socket is closed unexpectedly. NWFS is stopped.\n");
			return -1;
		} else if (read_cnt != sizeof(struct tz_fsdev_request_from_swd)) {
			tzdev_print(0, LOG_KTHREAD "Wrong packet size: %zd\n", read_cnt);
			continue;
		}

		if (kthread_should_stop())
			break;

		ret = tz_fsdev_put_command(iw_cmd_data.cmd, iw_cmd_data.session_id);
		if (ret)
			tzdev_print(0, LOG_KTHREAD "Cannot put command. ret = %d\n", ret);
	}

	return 0;
}

int tz_fsdev_initialize(void)
{
	task = kthread_run(&tz_fsdev_thread_function, NULL, "tz_fsdev");
	if (IS_ERR(task))
		return PTR_ERR(task);

	return 0;
}

static int __init tz_fsdev_init(void)
{
	int rc;

	rc = tz_cdev_register(&tz_fsdev_cdev);
	if (rc)
		return rc;

	nwfs_daemon_ctx = create_session_ctx();
	if (!nwfs_daemon_ctx) {
		tz_cdev_unregister(&tz_fsdev_cdev);
		tzdev_print(0, "NWFS Daemon context is not created. NWFS dev is not initialized\n");
		return -EFAULT;
	}

	nwfs_daemon_ctx->pid = -1;
	nwfs_daemon_ctx->session_id = 0;

	return 0;
}

static void __exit tz_fsdev_exit(void)
{
	tz_cdev_unregister(&tz_fsdev_cdev);

	kthread_stop(task);
	release_session_ctx(nwfs_daemon_ctx);
}

module_init(tz_fsdev_init);
module_exit(tz_fsdev_exit);
