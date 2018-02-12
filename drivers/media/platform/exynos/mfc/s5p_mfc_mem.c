/*
 * drivers/media/platform/exynos/mfc/s5p_mfc_mem.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/iommu.h>

#include "s5p_mfc_mem.h"

struct vb2_mem_ops *s5p_mfc_mem_ops(void)
{
	return (struct vb2_mem_ops *)&vb2_dma_sg_memops;
}

void s5p_mfc_mem_clean(struct s5p_mfc_dev *dev,
			struct s5p_mfc_special_buf *special_buf,
			off_t offset, size_t size)
{
	__dma_map_area(special_buf->vaddr + offset, size, DMA_TO_DEVICE);
	return;
}

void s5p_mfc_mem_invalidate(struct s5p_mfc_dev *dev,
			struct s5p_mfc_special_buf *special_buf,
			off_t offset, size_t size)
{
	__dma_map_area(special_buf->vaddr + offset, size, DMA_FROM_DEVICE);
	return;
}

int s5p_mfc_mem_get_user_shared_handle(struct s5p_mfc_ctx *ctx,
	struct mfc_user_shared_handle *handle)
{
	struct dma_buf *dma_buf;
	int ret = 0;
	int fd = handle->fd;

	handle->dma_buf = dma_buf_get(handle->fd);
	if (IS_ERR(handle->dma_buf)) {
		mfc_err_ctx("Failed to import fd\n");
		ret = PTR_ERR(handle->dma_buf);
		goto import_dma_fail;
	}

	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf)) {
		mfc_err_ctx("Faiiled to dma_buf_get (err %ld)\n", PTR_ERR(dma_buf));
		ret = -EINVAL;
		goto dma_buf_get_fail;
	}

	if (dma_buf->size < handle->data_size) {
		mfc_err_ctx("User-provided dma_buf size(%ld) is smaller than required size(%ld)\n",
				dma_buf->size, handle->data_size);
		ret = -EINVAL;
		goto dma_buf_size_fail;
	}

	handle->vaddr = dma_buf_vmap(handle->dma_buf);
	if (handle->vaddr == NULL) {
		mfc_err_ctx("Failed to get kernel virtual address\n");
		ret = -EINVAL;
		goto map_kernel_fail;
	}

	dma_buf_put(dma_buf);

	mfc_debug(2, "User Handle: fd = %d, virtual addr = 0x%p\n",
				handle->fd, handle->vaddr);

	return 0;

map_kernel_fail:
	handle->vaddr = NULL;
	dma_buf_put(handle->dma_buf);

dma_buf_size_fail:
	dma_buf_put(dma_buf);

dma_buf_get_fail:
	handle->vaddr = NULL;
	dma_buf_put(handle->dma_buf);

import_dma_fail:
	handle->dma_buf = NULL;
	handle->fd = -1;
	return ret;
}

void s5p_mfc_mem_cleanup_user_shared_handle(struct s5p_mfc_ctx *ctx,
		struct mfc_user_shared_handle *handle)
{

	if (handle->vaddr)
		dma_buf_vunmap(handle->dma_buf, handle->vaddr);
	if (handle->dma_buf)
		dma_buf_put(handle->dma_buf);

	handle->dma_buf = NULL;
	handle->vaddr = NULL;
	handle->fd = -1;
}

int s5p_mfc_mem_ion_alloc(struct s5p_mfc_dev *dev,
		struct s5p_mfc_special_buf *special_buf)
{
	struct s5p_mfc_ctx *ctx = dev->ctx[dev->curr_ctx];
	int flag;
	const char *heapname;

	switch (special_buf->buftype) {
	case MFCBUF_NORMAL:
		heapname = "ion_system_heap";
		flag = 0;
		break;
	case MFCBUF_NORMAL_FW:
		heapname = "vnfw_heap";
		flag = 0;
		break;
	case MFCBUF_DRM:
		heapname = "vframe_heap";
		flag = ION_FLAG_PROTECTED;
		break;
	case MFCBUF_DRM_FW:
		heapname = "vfw_heap";
		flag = ION_FLAG_PROTECTED;
		break;
	default:
		mfc_err_ctx("not supported mfc mem type: %d, heapname: %s\n",
				special_buf->buftype, heapname);
		return -EINVAL;
	}
	special_buf->dma_buf =
			ion_alloc_dmabuf(heapname, special_buf->size, flag);
	if (IS_ERR(special_buf->dma_buf)) {
		mfc_err_ctx("Failed to allocate buffer (err %ld)\n",
				PTR_ERR(special_buf->dma_buf));
		goto err_ion_alloc;
	}

	special_buf->attachment = dma_buf_attach(special_buf->dma_buf, dev->device);
	if (IS_ERR(special_buf->attachment)) {
		mfc_err_ctx("Failed to get dma_buf_attach (err %ld)\n",
				PTR_ERR(special_buf->attachment));
		goto err_attach;
	}

	special_buf->sgt = dma_buf_map_attachment(special_buf->attachment,
			DMA_BIDIRECTIONAL);
	if (IS_ERR(special_buf->sgt)) {
		mfc_err_ctx("Failed to get sgt (err %ld)\n",
				PTR_ERR(special_buf->sgt));
		goto err_map;
	}

	special_buf->daddr = ion_iovmm_map(special_buf->attachment, 0,
			special_buf->size, DMA_BIDIRECTIONAL, 0);
	if (IS_ERR_VALUE(special_buf->daddr)) {
		mfc_err_ctx("Failed to allocate iova (err 0x%p)\n",
				&special_buf->daddr);
		goto err_iovmm;
	}

	special_buf->vaddr = dma_buf_vmap(special_buf->dma_buf);
	if (IS_ERR(special_buf->vaddr)) {
		mfc_err_ctx("Failed to get vaddr (err 0x%p)\n",
				&special_buf->vaddr);
		goto err_vaddr;
	}

	return 0;
err_vaddr:
	special_buf->vaddr = NULL;
	ion_iovmm_unmap(special_buf->attachment, special_buf->daddr);

err_iovmm:
	special_buf->daddr = 0;
	dma_buf_unmap_attachment(special_buf->attachment, special_buf->sgt,
				 DMA_BIDIRECTIONAL);
err_map:
	special_buf->sgt = NULL;
	dma_buf_detach(special_buf->dma_buf, special_buf->attachment);
err_attach:
	special_buf->attachment = NULL;
	dma_buf_put(special_buf->dma_buf);
err_ion_alloc:
	special_buf->dma_buf = NULL;
	return -ENOMEM;
}

void s5p_mfc_mem_ion_free(struct s5p_mfc_dev *dev,
		struct s5p_mfc_special_buf *special_buf)
{
	if (special_buf->vaddr)
		dma_buf_vunmap(special_buf->dma_buf, special_buf->vaddr);
	if (special_buf->daddr)
		ion_iovmm_unmap(special_buf->attachment, special_buf->daddr);
	if (special_buf->sgt)
		dma_buf_unmap_attachment(special_buf->attachment,
					 special_buf->sgt, DMA_BIDIRECTIONAL);
	if (special_buf->attachment)
		dma_buf_detach(special_buf->dma_buf, special_buf->attachment);
	if (special_buf->dma_buf)
		dma_buf_put(special_buf->dma_buf);

	special_buf->dma_buf = NULL;
	special_buf->attachment = NULL;
	special_buf->sgt = NULL;
	special_buf->daddr = 0;
	special_buf->vaddr = NULL;
}

void s5p_mfc_put_iovmm(struct s5p_mfc_ctx *ctx, int num_planes, int index, int spare)
{
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	int i;

	for (i = 0; i < num_planes; i++) {
		if (dec->assigned_addr[index][spare][i]) {
			mfc_debug(2, "[IOVMM] index %d buf[%d] addr: %#llx\n",
					index, i, dec->assigned_addr[index][spare][i]);
			ion_iovmm_unmap(dec->assigned_attach[index][spare][i], dec->assigned_addr[index][spare][i]);
		}
		if (dec->assigned_attach[index][spare][i])
			dma_buf_detach(dec->assigned_dmabufs[index][spare][i], dec->assigned_attach[index][spare][i]);
		if (dec->assigned_dmabufs[index][spare][i])
			dma_buf_put(dec->assigned_dmabufs[index][spare][i]);

		dec->assigned_addr[index][spare][i] = 0;
		dec->assigned_attach[index][spare][i] = NULL;
		dec->assigned_dmabufs[index][spare][i] = NULL;
	}

	dec->assigned_refcnt[index]--;
	mfc_debug(2, "[IOVMM] index %d ref %d\n", index, dec->assigned_refcnt[index]);
}

void s5p_mfc_get_iovmm(struct s5p_mfc_ctx *ctx, struct vb2_buffer *vb)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	int i, mem_get_count = 0;
	int index = vb->index;
	int ioprot = IOMMU_WRITE;

	for (i = 0; i < ctx->dst_fmt->mem_planes; i++) {
		mem_get_count++;

		dec->assigned_dmabufs[index][0][i] = dma_buf_get(vb->planes[i].m.fd);
		if (IS_ERR(dec->assigned_dmabufs[index][0][i])) {
			mfc_err_ctx("[IOVMM] Failed to dma_buf_get (err %ld)\n",
					PTR_ERR(dec->assigned_dmabufs[index][0][i]));
			dec->assigned_dmabufs[index][0][i] = NULL;
			goto err_iovmm;
		}

		dec->assigned_attach[index][0][i] = dma_buf_attach(dec->assigned_dmabufs[index][0][i], dev->device);
		if (IS_ERR(dec->assigned_attach[index][0][i])) {
			mfc_err_ctx("[IOVMM] Failed to get dma_buf_attach (err %ld)\n",
					PTR_ERR(dec->assigned_attach[index][0][i]));
			dec->assigned_attach[index][0][i] = NULL;
			goto err_iovmm;
		}

		if (device_get_dma_attr(dev->device) == DEV_DMA_COHERENT)
			ioprot |= IOMMU_CACHE;

		dec->assigned_addr[index][0][i] = ion_iovmm_map(dec->assigned_attach[index][0][i],
				0, ctx->raw_buf.plane_size[i], DMA_FROM_DEVICE, ioprot);
		if (IS_ERR_VALUE(dec->assigned_addr[index][0][i])) {
			mfc_err_ctx("[IOVMM] Failed to allocate iova (err 0x%p)\n",
					&dec->assigned_addr[index][0][i]);
			dec->assigned_addr[index][0][i] = 0;
			goto err_iovmm;
		}
		mfc_debug(2, "[IOVMM] index %d buf[%d] addr: %#llx\n",
				index, i, dec->assigned_addr[index][0][i]);
	}

	dec->assigned_refcnt[index]++;
	mfc_debug(2, "[IOVMM] index %d ref %d\n", index, dec->assigned_refcnt[index]);

	return;

err_iovmm:
	dec->assigned_refcnt[index]++;
	s5p_mfc_put_iovmm(ctx, mem_get_count, index, 0);
}

void s5p_mfc_move_iovmm_to_spare(struct s5p_mfc_ctx *ctx, int num_planes, int index)
{
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	int i;

	for (i = 0; i < num_planes; i++) {
		dec->assigned_addr[index][1][i] = dec->assigned_addr[index][0][i];
		dec->assigned_attach[index][1][i] = dec->assigned_attach[index][0][i];
		dec->assigned_dmabufs[index][1][i] = dec->assigned_dmabufs[index][0][i];
		dec->assigned_addr[index][0][i] = 0;
		dec->assigned_attach[index][0][i] = NULL;
		dec->assigned_dmabufs[index][0][i] = NULL;
	}

	mfc_debug(2, "[IOVMM] moved DPB[%d] to spare\n", index);
}

void s5p_mfc_cleanup_assigned_iovmm(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_dec *dec = ctx->dec_priv;
	int i;

	mutex_lock(&dec->dpb_mutex);

	for (i = 0; i < MFC_MAX_DPBS; i++) {
		if (dec->assigned_refcnt[i] == 0) {
			continue;
		} else if (dec->assigned_refcnt[i] == 1) {
			s5p_mfc_put_iovmm(ctx, ctx->dst_fmt->mem_planes, i, 0);
		} else if (dec->assigned_refcnt[i] == 2) {
			s5p_mfc_put_iovmm(ctx, ctx->dst_fmt->mem_planes, i, 0);
			s5p_mfc_put_iovmm(ctx, ctx->dst_fmt->mem_planes, i, 1);
		} else {
			mfc_err_ctx("[IOVMM] index %d invalid refcnt %d\n", i, dec->assigned_refcnt[i]);
			MFC_TRACE_CTX("DPB[%d] invalid refcnt %d\n",
					i, dec->assigned_refcnt[i]);
		}
	}

	mutex_unlock(&dec->dpb_mutex);
}
