/*
 * Copyright (C) 2018 Loongson Inc.
 * Author:  Bibo Mao, maobibo@loongson.cn
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/printk.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/notifier.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_para.h>
#include "rtasd.h"

static void rtas_event_scan(struct work_struct *w);
DECLARE_DELAYED_WORK(event_scan_work, rtas_event_scan);

/* RTAS service tokens */
static unsigned int rtas_error_log_max;
static unsigned int rtas_error_log_buffer_max;
static unsigned int rtas_event_scan_rate;
static unsigned char logdata[RTAS_ERROR_LOG_MAX];
static unsigned int event_scan;
static char *rtas_log_buf;
static unsigned long rtas_log_start;
static unsigned long rtas_log_size;

static int error_log_cnt = 0;
static struct rtas_event_log_hp response;
typedef struct rtas_reply{
	struct list_head         entry;
	struct rtas_event_log_hp response;
        int sect_index;
	int times;
}rtas_reply;

static DEFINE_SPINLOCK(rtasd_log_lock);
static LIST_HEAD(pending_reply_list);

/* hack here, online memory block should be called in kernel
 */
bool pages_correctly_reserved(unsigned long start_pfn)
{
	int i, j;
	struct page *page;
	unsigned long pfn = start_pfn;

	/*
	 * memmap between sections is not contiguous except with
	 * SPARSEMEM_VMEMMAP. We lookup the page once per section
	 * and assume memmap is contiguous within each section
	 */
	for (i = 0; i < 1; i++, pfn += PAGES_PER_SECTION) {
		if (WARN_ON_ONCE(!pfn_valid(pfn)))
			return false;
		page = pfn_to_page(pfn);

		for (j = 0; j < PAGES_PER_SECTION; j++) {
			if (PageReserved(page + j))
				continue;

			printk(KERN_WARNING "section number %ld page number %d "
					"not reserved, was it already online?\n",
					pfn_to_section_nr(pfn), j);

			return false;
		}
	}

	return true;
}

int memory_block_action(unsigned long phys_index, unsigned long action)
{
	unsigned long start_pfn;
	unsigned long nr_pages = PAGES_PER_SECTION;
	struct page *first_page;
	int ret, online_type;

	first_page = pfn_to_page(phys_index << PFN_SECTION_SHIFT);
	start_pfn = page_to_pfn(first_page);
	online_type = ONLINE_KEEP;

	switch (action) {
		case MEM_ONLINE:
			if (!pages_correctly_reserved(start_pfn))
				return -EBUSY;

			ret = online_pages(start_pfn, nr_pages, online_type);
			break;
		case MEM_OFFLINE:
			ret = offline_pages(start_pfn, nr_pages);
			break;
		default:
			WARN(1, KERN_WARNING "%s(%ld, %ld) unknown action: "
					"%ld\n", __func__, phys_index, action, action);
			ret = -EINVAL;
	}

	return ret;
}

static int rtas_call(int token, unsigned int eventmask, unsigned long pa)
{
	int ret;

	ret = kvm_hypercall3(KVM_MIPS_GET_RTAS_INFO, token, eventmask, pa);
	return ret;
}

void clear_cpu_topology(int cpu);
void setup_cpu_topology(int cpu);

void handle_rtas_event(const struct rtas_error_log *log)
{
	struct rtas_event_log_hp *hp;
	unsigned long start_addr, len, sect_index;
	int ret, node, cpu;
	rtas_reply *reply_node;

	if (log->type != RTAS_TYPE_HOTPLUG)
		return;

	hp = (struct rtas_event_log_hp *)(log->buffer);
	printk("handle_rtas_event1 type %x action %d  index 0x%x count 0x%x\n",
		hp->hotplug_type, hp->hotplug_action, hp->index, hp->count);
	if (hp->hotplug_type == RTAS_HP_TYPE_MEMORY) {
		start_addr = (unsigned long)(hp->index) << SECTION_SIZE_BITS;
		len = (unsigned long)(hp->count) << SECTION_SIZE_BITS;
		if (RTAS_HP_ACTION_ADD == hp->hotplug_action) {
			/* add memory here */
			node = memory_add_physaddr_to_nid(start_addr);
			ret = add_memory(node, start_addr, len);
			if (ret && ret != -EEXIST) {
				printk(KERN_ERR "Fail to add memory start %lx len %lx\n", start_addr, len);
				return;
			}

			sect_index = 0;
			while (sect_index < hp->count) {
				memory_block_action(hp->index + sect_index, MEM_ONLINE);
				sect_index++;
			}
		} else if (RTAS_HP_ACTION_REMOVE == hp->hotplug_action) {
			reply_node = kmalloc(sizeof(rtas_reply), GFP_KERNEL);
			if (reply_node == NULL)
				return;
			memcpy(&reply_node->response, hp, sizeof(struct rtas_event_log_hp));
			reply_node->times = 0;
			reply_node->sect_index = 0;
			list_add_tail(&reply_node->entry, &pending_reply_list);
		}
	} else if (hp->hotplug_type == RTAS_HP_TYPE_CPU) {
		cpu = hp->index;
		if (RTAS_HP_ACTION_ADD == hp->hotplug_action) {
			set_cpu_present(cpu,true);
			cpu_set(cpu, __node_data[(cpu/cores_per_node)]->cpumask);
			setup_cpu_topology(cpu);
			cpu_up(cpu);
			get_cpu_device(cpu)->offline = false;

		} else if (RTAS_HP_ACTION_REMOVE == hp->hotplug_action) {
			cpu_down(cpu);
			set_cpu_present(cpu,false);
			get_cpu_device(cpu)->offline = true;
			cpu_clear(cpu, __node_data[(cpu/cores_per_node)]->cpumask);
			clear_cpu_topology(cpu);

			reply_node = kmalloc(sizeof(rtas_reply), GFP_KERNEL);
			if (reply_node == NULL)
				return;
			memcpy(&reply_node->response, hp, sizeof(struct rtas_event_log_hp));
			reply_node->times = 0;
			reply_node->sect_index = 0;
			list_add_tail(&reply_node->entry, &pending_reply_list);
		}

	}
}

static void handle_rtas_reply(void)
{
	struct list_head *node, *next;
	rtas_reply *reply_node;
	struct rtas_event_log_hp *hp;
	unsigned long start_addr, len, sect_index;
	int ret, nid;

	if (list_empty(&pending_reply_list))
		return;

	list_for_each_safe(node, next, &pending_reply_list) {
		reply_node = container_of(node, rtas_reply, entry);
		hp = &reply_node->response;
		if (hp->hotplug_type == RTAS_HP_TYPE_MEMORY) {
			start_addr = (unsigned long)(hp->index) << SECTION_SIZE_BITS;
			len = (unsigned long)(hp->count) << SECTION_SIZE_BITS;
			if (RTAS_HP_ACTION_REMOVE == hp->hotplug_action) {

				sect_index = reply_node->sect_index;
				ret = 0;
				while ((ret == 0) && (sect_index < hp->count)) {
					ret = memory_block_action(hp->index + sect_index, MEM_OFFLINE);
					if (ret == 0)
						reply_node->sect_index = ++sect_index;
				}

				if (sect_index == hp->count) {
					nid = memory_add_physaddr_to_nid(start_addr);
					remove_memory(nid, start_addr, len);
					memcpy(&response, hp, sizeof(struct rtas_event_log_hp));
					rtas_call(RTAS_EVENT_REPLY, RTAS_EVENT_SCAN_ALL_EVENTS, __pa(&response));
					list_del(node);
					kfree(reply_node);
					printk( "Succeed to offline memory start %lx len %lx\n", start_addr, len);
				} else {
					printk(KERN_ERR "Fail to offline memory start %lx len %lx ret %d\n", start_addr, len, ret);
					reply_node->times++;
				}
			}
		} else if (hp->hotplug_type == RTAS_HP_TYPE_CPU) {
			if (RTAS_HP_ACTION_REMOVE == hp->hotplug_action) {
				memcpy(&response, hp, sizeof(struct rtas_event_log_hp));
				rtas_call(RTAS_EVENT_REPLY, RTAS_EVENT_SCAN_ALL_EVENTS, __pa(&response));
				list_del(node);
				kfree(reply_node);

			}

		}
	}
}

static int log_rtas_len(char * buf)
{
	int len;
	struct rtas_error_log *err;
	unsigned int extended_log_length;

	/* rtas fixed header */
	len = 8;
	err = (struct rtas_error_log *)buf;
	extended_log_length = rtas_error_extended_log_length(err);
	if (rtas_error_extended(err) && extended_log_length) {

		/* extended header */
		len += extended_log_length;
	}

	if (len > rtas_error_log_max)
		len = rtas_error_log_max;

	return len;
}

static void rtas_log_error(char *buf, unsigned int err_type, int fatal)
{
	unsigned long offset;
	unsigned long s;
	int len = 0;

	pr_debug("rtasd: logging event\n");
	if (buf == NULL)
		return;

	spin_lock_irqsave(&rtasd_log_lock, s);

	/* get length and increase count */
	len = log_rtas_len(buf);
	error_log_cnt++;

	/*
	 * rtas errors can occur during boot, and we do want to capture
	 * those somewhere, even if nvram isn't ready (why not?), and even
	 * if rtasd isn't ready. Put them into the boot log, at least.
	 */
	offset = rtas_error_log_buffer_max *
		((rtas_log_start+rtas_log_size) & LOG_NUMBER_MASK);

	/* First copy over sequence number */
	memcpy(&rtas_log_buf[offset], (void *) &error_log_cnt, sizeof(int));

	/* Second copy over error log data */
	offset += sizeof(int);
	memcpy(&rtas_log_buf[offset], buf, len);

	if (rtas_log_size < LOG_NUMBER)
		rtas_log_size += 1;
	else
		rtas_log_start += 1;

	spin_unlock_irqrestore(&rtasd_log_lock, s);
	// wake_up_interruptible(&rtas_log_wait);
}

static void do_event_scan(void)
{
	int error;
	do {
		memset(logdata, 0, rtas_error_log_max);
		error = rtas_call(event_scan, RTAS_EVENT_SCAN_ALL_EVENTS, __pa(logdata));
		if (error == 0) {
			rtas_log_error(logdata, ERR_TYPE_RTAS_LOG, 0);
			handle_rtas_event((struct rtas_error_log *)logdata);
		}

		handle_rtas_reply();
	} while(error == 0);
}

static void rtas_event_scan(struct work_struct *w)
{
	do_event_scan();
}

/* Cancel the rtas event scan work */
void rtas_cancel_event_scan(void)
{
	cancel_delayed_work_sync(&event_scan_work);
}
EXPORT_SYMBOL_GPL(rtas_cancel_event_scan);

extern int register_mips_hotplug_notifier(struct notifier_block *nb);
static int hotplug_notifier_call(struct notifier_block *,
                                  unsigned long code, void *param);
static struct notifier_block hotplug_notifier_block = {
        .notifier_call = hotplug_notifier_call,
};

static int hotplug_notifier_call(struct notifier_block *nb,
                                  unsigned long code, void *_param)
{
	queue_delayed_work_on(0, system_wq, &event_scan_work, 0);
	return NOTIFY_OK;
}

static int __init rtas_event_scan_init(void)
{
	/* Make room for the sequence number */
	event_scan = RTAS_EVENT_SCAN;
	rtas_event_scan_rate = RTAS_EVENT_SCAN_RATE;
	rtas_error_log_max = RTAS_ERROR_LOG_MAX;
	rtas_error_log_buffer_max = rtas_error_log_max + sizeof(int);

	rtas_log_buf = vmalloc(rtas_error_log_buffer_max*LOG_NUMBER);
	if (!rtas_log_buf) {
		printk(KERN_ERR "rtasd: no memory\n");
		return -ENOMEM;
	}

	register_mips_hotplug_notifier(&hotplug_notifier_block);
	return 0;
}
arch_initcall(rtas_event_scan_init);
