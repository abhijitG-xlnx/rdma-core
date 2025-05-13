// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018-2025 Advanced Micro Devices, Inc.  All rights reserved.
 */

#include <errno.h>
#include <string.h>

#include "ionic.h"
#include "ionic_queue.h"
#include "ionic_memory.h"

static int ionic_order_base2(size_t val)
{
	int order = 32;

	if (val <= 1)
		return 0;

	val -= 1;

	val |= val >> 1;
	val |= val >> 2;
	val |= val >> 4;
	val |= val >> 8;
	val |= val >> 16;

	/* leave zero, exactly one high bit set, or exactly one low bit set */
	val ^= val >> 1;

	if (sizeof(size_t) == 8)
		order += ffs(val >> 32);
	if (order == 32)
		order = ffs(val);

	return order;
}

static void ionic_queue_map(struct ionic_queue *q, struct ionic_pd *pd, uint64_t pd_tag, int stride)
{
	if (pd && pd->alloc) {
		size_t align = IONIC_PAGE_SIZE;
		if (align < stride)
			align = stride;
		q->ptr = pd->alloc(&pd->ibpd, pd->pd_context, q->size, align, pd_tag);
		if (q->ptr != IBV_ALLOCATOR_USE_DEFAULT) {
			q->pd = pd;
			q->pd_tag = pd_tag;
			return;
		}
	}

	q->ptr = ionic_map_anon(q->size);
}

static void ionic_queue_unmap(struct ionic_queue *q)
{
	struct ionic_pd *pd = q->pd;

	if (pd) {
		pd->free(&pd->ibpd, pd->pd_context, q->ptr, q->pd_tag);
		q->ptr = NULL;
		q->pd = NULL;
		q->pd_tag = 0;
		return;
	}

	ionic_unmap(q->ptr, q->size);
}

int ionic_queue_init(struct ionic_queue *q, struct ionic_pd *pd,
		     uint64_t pd_tag, int pg_shift, int depth, size_t stride)
{
	if (depth < 0 || depth > 0xffff)
		return -EINVAL;

	if (stride == 0 || stride > 0x10000)
		return -EINVAL;

	if (depth == 0)
		depth = 1;

	q->depth_log2 = ionic_order_base2(depth + 1);
	q->stride_log2 = ionic_order_base2(stride);

	if (q->depth_log2 + q->stride_log2 < pg_shift)
		q->depth_log2 = pg_shift - q->stride_log2;

	q->size = 1ull << (q->depth_log2 + q->stride_log2);
	q->mask = (1u << q->depth_log2) - 1;

	ionic_queue_map(q, pd, pd_tag, stride);
	if (!q->ptr)
		return errno;

	q->prod = 0;
	q->cons = 0;
	q->dbell = 0;

	return 0;
}

void ionic_queue_destroy(struct ionic_queue *q)
{
	ionic_queue_unmap(q);
}
