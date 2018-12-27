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
static unsigned long event_scan_delay = 1*HZ;
static int first_pass = 1;
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

void handle_rtas_event(const struct rtas_error_log *log)
{
	struct rtas_event_log_hp *hp;
	unsigned long start_addr, len, start_section;
	int ret, node;
	rtas_reply *reply_node;

	if (log->type != RTAS_TYPE_HOTPLUG)
		return;

	hp = (struct rtas_event_log_hp *)(log->buffer);
	printk("handle_rtas_event1 type %x action %d  index %x count %x\n", hp->hotplug_type, hp->hotplug_action, hp->index, hp->count);
	if (hp->hotplug_type == RTAS_HP_TYPE_MEMORY) {
		start_addr = hp->index << SECTION_SIZE_BITS;
		len = hp->count << SECTION_SIZE_BITS;
		if (RTAS_HP_ACTION_ADD == hp->hotplug_action) {
			/* add memory here */
			node = memory_add_physaddr_to_nid(start_addr);
			ret = add_memory(node, start_addr, len);
			if (ret && ret != -EEXIST) {
				printk(KERN_ERR "Fail to add memory start %lx len %lx\n", start_addr, len);
				return;
			}

			start_section =  start_addr >> SECTION_SIZE_BITS;
			memory_block_action(start_section, MEM_ONLINE);
		} else if (RTAS_HP_ACTION_REMOVE == hp->hotplug_action) {
			reply_node = vmalloc(sizeof(rtas_reply));
			if (reply_node == NULL)
				return;
			memcpy(&reply_node->response, hp, sizeof(struct rtas_event_log_hp));
			reply_node->times = 0;
			list_add_tail(&reply_node->entry, &pending_reply_list);
		}
	}
}

void handle_rtas_reply(void)
{
	struct list_head *node, *next;
	rtas_reply *reply_node;
	struct rtas_event_log_hp *hp;
	unsigned long start_addr, len, start_section;
	int ret, nid;

	if (list_empty(&pending_reply_list))
		return;

	list_for_each_safe(node, next, &pending_reply_list) {
		reply_node = container_of(node, rtas_reply, entry);
		hp = &reply_node->response;
		if (hp->hotplug_type == RTAS_HP_TYPE_MEMORY) {
			start_addr = hp->index << SECTION_SIZE_BITS;
			len = hp->count << SECTION_SIZE_BITS;
			if (RTAS_HP_ACTION_REMOVE == hp->hotplug_action) {
				start_section =  start_addr >> SECTION_SIZE_BITS;
				ret = memory_block_action(start_section, MEM_OFFLINE);
				if (ret == 0) {
					nid = memory_add_physaddr_to_nid(start_addr);
					remove_memory(nid, start_addr, len);
					memcpy(&response, hp, sizeof(struct rtas_event_log_hp));
					rtas_call(RTAS_EVENT_REPLY, RTAS_EVENT_SCAN_ALL_EVENTS, __pa(&response));
					list_del(node);
					vfree(reply_node);
					printk( "Succeed to offline memory start %lx len %lx\n", start_addr, len);
				} else {
					memory_block_action(start_section, MEM_ONLINE);
					printk(KERN_ERR "Fail to offline memory start %lx len %lx ret %d\n", start_addr, len, ret);
					reply_node->times++;
				}
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
		handle_rtas_reply();
		memset(logdata, 0, rtas_error_log_max);
		error = rtas_call(event_scan, RTAS_EVENT_SCAN_ALL_EVENTS, __pa(logdata));
		if (error == -1) {
			break;
		}

		if (error == 0) {
			rtas_log_error(logdata, ERR_TYPE_RTAS_LOG, 0);
			handle_rtas_event((struct rtas_error_log *)logdata);
		}

	} while(error == 0);
}

static void rtas_event_scan(struct work_struct *w)
{
	unsigned int cpu;

	do_event_scan();
	get_online_cpus();

	/* raw_ OK because just using CPU as starting point. */
	cpu = cpumask_next(raw_smp_processor_id(), cpu_online_mask);
	if (cpu >= nr_cpu_ids) {
		cpu = cpumask_first(cpu_online_mask);

		if (first_pass) {
			first_pass = 0;
			event_scan_delay = 30*HZ/rtas_event_scan_rate;
		}
	}

	schedule_delayed_work_on(cpu, &event_scan_work,
			__round_jiffies_relative(event_scan_delay, cpu));
	put_online_cpus();
}

/* Cancel the rtas event scan work */
void rtas_cancel_event_scan(void)
{
	cancel_delayed_work_sync(&event_scan_work);
}
EXPORT_SYMBOL_GPL(rtas_cancel_event_scan);

static void start_event_scan(void)
{
	printk(KERN_DEBUG "RTAS daemon started\n");
	pr_debug("rtasd: will sleep for %d milliseconds\n",
			(30000 / rtas_event_scan_rate));

	schedule_delayed_work_on(cpumask_first(cpu_online_mask),
			&event_scan_work, event_scan_delay);
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

	start_event_scan();
	return 0;
}
arch_initcall(rtas_event_scan_init);
