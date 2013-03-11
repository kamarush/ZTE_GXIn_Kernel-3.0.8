/**************************************************************************
 * MSVDX I/O operations and IRQ handling
 *
 * Copyright (c) 2007 Intel Corporation, Hillsboro, OR, USA
 * Copyright (c) Imagination Technologies Limited, UK
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#include <drm/drmP.h>
#include "psb_drm.h"
#include "psb_drv.h"
#include "psb_msvdx.h"
#include "pnw_topaz.h"
#include "psb_powermgmt.h"
#include <linux/io.h>
#include <linux/delay.h>

#ifndef list_first_entry
#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)
#endif

static int ied_enabled;

static int psb_msvdx_send(struct drm_device *dev, void *cmd,
			  unsigned long cmd_size);

static int psb_msvdx_dequeue_send(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_msvdx_cmd_queue *msvdx_cmd = NULL;
	int ret = 0;
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;
	unsigned long irq_flags;

	spin_lock_irqsave(&msvdx_priv->msvdx_lock, irq_flags);
	if (list_empty(&msvdx_priv->msvdx_queue)) {
		PSB_DEBUG_GENERAL("MSVDXQUE: msvdx list empty.\n");
		msvdx_priv->msvdx_busy = 0;
		spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);
		return -EINVAL;
	}

	msvdx_cmd = list_first_entry(&msvdx_priv->msvdx_queue,
				     struct psb_msvdx_cmd_queue, head);
	list_del(&msvdx_cmd->head);
	spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);

	PSB_DEBUG_GENERAL("MSVDXQUE: Queue has id %08x\n", msvdx_cmd->sequence);
	ret = psb_msvdx_send(dev, msvdx_cmd->cmd, msvdx_cmd->cmd_size);
	if (ret) {
		DRM_ERROR("MSVDXQUE: psb_msvdx_send failed\n");
		ret = -EINVAL;
	}

	kfree(msvdx_cmd->cmd);
	kfree(msvdx_cmd);

	return ret;
}

static int psb_msvdx_map_command(struct drm_device *dev,
				 struct ttm_buffer_object *cmd_buffer,
				 unsigned long cmd_offset, unsigned long cmd_size,
				 void **msvdx_cmd, uint32_t sequence, int copy_cmd)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;
	int ret = 0;
	unsigned long cmd_page_offset = cmd_offset & ~PAGE_MASK;
	unsigned long cmd_size_remaining;
	struct ttm_bo_kmap_obj cmd_kmap, regio_kmap;
	void *cmd, *cmd_copy, *cmd_start;
	bool is_iomem;
	int i;
	int first_empty = -1;


	/* command buffers may not exceed page boundary */
	if ((cmd_size > PAGE_SIZE) || (cmd_size + cmd_page_offset > PAGE_SIZE))
		return -EINVAL;

	ret = ttm_bo_kmap(cmd_buffer, cmd_offset >> PAGE_SHIFT, 1, &cmd_kmap);
	if (ret) {
		DRM_ERROR("MSVDXQUE:ret:%d\n", ret);
		return ret;
	}

	cmd_start = (void *)ttm_kmap_obj_virtual(&cmd_kmap, &is_iomem)
		    + cmd_page_offset;
	cmd = cmd_start;
	cmd_size_remaining = cmd_size;

	while (cmd_size_remaining > 0) {
		if (cmd_size_remaining < FWRK_GENMSG_HEADER_SIZE)
			return -EINVAL;
		uint32_t cur_cmd_size = MEMIO_READ_FIELD(cmd, FWRK_GENMSG_SIZE);
		uint32_t cur_cmd_id = MEMIO_READ_FIELD(cmd, FWRK_GENMSG_ID);
		uint32_t mmu_ptd = 0, msvdx_mmu_invalid = 0;
		unsigned long irq_flags;

		PSB_DEBUG_GENERAL("cmd start at %08x cur_cmd_size = %d"
				  " cur_cmd_id = %02x fence = %08x\n",
				  (uint32_t) cmd, cur_cmd_size, cur_cmd_id, sequence);
		if (cur_cmd_size % sizeof(uint32_t)) {
			ret = -EINVAL;
			DRM_ERROR("MSVDX: msg size isn't 32 bits aligned.\n");
			goto out;
		}
		if (cur_cmd_size > cmd_size_remaining) {
			ret = -EINVAL;
			DRM_ERROR("MSVDX: msg size is not correct.\n");
			goto out;
		}

		switch (cur_cmd_id) {
		case VA_MSGID_RENDER:
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: send render message.\n");
			if (cur_cmd_size != FW_VA_RENDER_SIZE) {
				/* Msg size is not correct */
				ret = -EINVAL;
				PSB_DEBUG_MSVDX("MSVDX: wrong msg size.\n");
				goto out;
			}

			/* Fence ID */
			if (IS_MDFLD(dev) && IS_FW_UPDATED)
				MEMIO_WRITE_FIELD(cmd, FW_VA_DECODE_MSG_ID, sequence);
			else
				MEMIO_WRITE_FIELD(cmd, FW_VA_RENDER_FENCE_VALUE, sequence);

			mmu_ptd = psb_get_default_pd_addr(dev_priv->mmu);
			msvdx_mmu_invalid = atomic_cmpxchg(&dev_priv->msvdx_mmu_invaldc,
							   1, 0);
			if (msvdx_mmu_invalid == 1) {
				if (!(IS_MDFLD(dev) && IS_FW_UPDATED))
					mmu_ptd |= 1;
				else {
					uint32_t flags;
					flags = MEMIO_READ_FIELD(cmd, FW_DEVA_DECODE_FLAGS);
					flags |= FW_DEVA_INVALIDATE_MMU;
					MEMIO_WRITE_FIELD(cmd, FW_DEVA_DECODE_FLAGS, flags);
					psb_gl3_global_invalidation(dev);
				}

				PSB_DEBUG_GENERAL("MSVDX:Set MMU invalidate\n");
			}

			/* PTD */
			if (IS_MDFLD(dev) && IS_FW_UPDATED) {
				uint32_t context_id;
				context_id = MEMIO_READ_FIELD(cmd, FW_VA_DECODE_MMUPTD);
				mmu_ptd = mmu_ptd | (context_id & 0xff);
				MEMIO_WRITE_FIELD(cmd, FW_VA_DECODE_MMUPTD, mmu_ptd);
			} else
				MEMIO_WRITE_FIELD(cmd, FW_VA_RENDER_MMUPTD, mmu_ptd);
			break;

		case VA_MSGID_OOLD_MFLD:
		case VA_MSGID_DEBLOCK_MFLD: {
			FW_VA_DEBLOCK_MSG * deblock_msg;
			if (cur_cmd_size != FW_VA_DEBLOCK_SIZE) {
				/* Msg size is not correct */
				ret = -EINVAL;
				PSB_DEBUG_MSVDX("MSVDX: wrong msg size.\n");
				goto out;
			}

			PSB_DEBUG_GENERAL("MSVDX:Get deblock cmd for medfield\n");

			deblock_msg = (FW_VA_DEBLOCK_MSG *)cmd;

			mmu_ptd = psb_get_default_pd_addr(dev_priv->mmu);
			msvdx_mmu_invalid = atomic_cmpxchg(&dev_priv->msvdx_mmu_invaldc,
							   1, 0);
			if (msvdx_mmu_invalid == 1) {
				uint32_t flags;
				flags = deblock_msg->flags;
				flags |= FW_DEVA_INVALIDATE_MMU;
				deblock_msg->flags = flags;

				PSB_DEBUG_GENERAL("MSVDX:Set MMU invalidate\n");
			}


			deblock_msg->header.bits.msg_type = cur_cmd_id - VA_MSGID_DEBLOCK_MFLD + VA_MSGID_DEBLOCK; /* patch to right cmd type */
			deblock_msg->header.bits.msg_fence = (uint16_t)(sequence & 0xffff);
			deblock_msg->mmu_context.bits.mmu_ptd = (mmu_ptd >> 8);

		}
		break;

		default:
			/* Msg not supported */
			ret = -EINVAL;
			PSB_DEBUG_GENERAL("MSVDX: ret:%d\n", ret);
			goto out;
		}

		cmd += cur_cmd_size;
		cmd_size_remaining -= cur_cmd_size;
	}

	if (copy_cmd) {
		PSB_DEBUG_GENERAL("MSVDXQUE:copying command\n");

		cmd_copy = kzalloc(cmd_size, GFP_KERNEL);
		if (cmd_copy == NULL) {
			ret = -ENOMEM;
			DRM_ERROR("MSVDX: fail to callc,ret=:%d\n", ret);
			goto out;
		}
		memcpy(cmd_copy, cmd_start, cmd_size);
		*msvdx_cmd = cmd_copy;
	} else {
		PSB_DEBUG_GENERAL("MSVDXQUE:did NOT copy command\n");
		ret = psb_msvdx_send(dev, cmd_start, cmd_size);
		if (ret) {
			DRM_ERROR("MSVDXQUE: psb_msvdx_send failed\n");
			ret = -EINVAL;
		}
	}

out:
	ttm_bo_kunmap(&cmd_kmap);

	return ret;
}

int psb_submit_video_cmdbuf(struct drm_device *dev,
			    struct ttm_buffer_object *cmd_buffer,
			    unsigned long cmd_offset, unsigned long cmd_size,
			    struct ttm_fence_object *fence)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	uint32_t sequence =  dev_priv->sequence[PSB_ENGINE_VIDEO];
	unsigned long irq_flags;
	int ret = 0;
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;
	int offset = 0;
	/* psb_schedule_watchdog(dev_priv); */

	spin_lock_irqsave(&msvdx_priv->msvdx_lock, irq_flags);

	dev_priv->last_msvdx_ctx = dev_priv->msvdx_ctx;

	if (msvdx_priv->msvdx_needs_reset) {
		spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);
		PSB_DEBUG_GENERAL("MSVDX: will reset msvdx\n");
		if (!IS_D0(dev)) {
			if (psb_msvdx_reset(dev_priv)) {
				ret = -EBUSY;
				DRM_ERROR("MSVDX: Reset failed\n");
				return ret;
			}
		}
		msvdx_priv->msvdx_needs_reset = 0;
		msvdx_priv->msvdx_busy = 0;

		psb_msvdx_init(dev);

		/* restore vec local mem if needed */
		if (msvdx_priv->vec_local_mem_saved) {
			for (offset = 0; offset < VEC_LOCAL_MEM_BYTE_SIZE / 4; ++offset)
				PSB_WMSVDX32(msvdx_priv->vec_local_mem_data[offset],
					     VEC_LOCAL_MEM_OFFSET + offset * 4);

			msvdx_priv->vec_local_mem_saved = 0;
		}

		spin_lock_irqsave(&msvdx_priv->msvdx_lock, irq_flags);
	}

	if (!msvdx_priv->msvdx_fw_loaded) {
		spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);
		PSB_DEBUG_GENERAL("MSVDX:reload FW to MTX\n");

		ret = psb_setup_fw(dev);
		if (ret) {
			DRM_ERROR("MSVDX:fail to load FW\n");
			/* FIXME: find a proper return value */
			return -EFAULT;
		}
		msvdx_priv->msvdx_fw_loaded = 1;

		PSB_DEBUG_GENERAL("MSVDX: load firmware successfully\n");
		spin_lock_irqsave(&msvdx_priv->msvdx_lock, irq_flags);
	}

	if (!msvdx_priv->msvdx_busy) {
		msvdx_priv->msvdx_busy = 1;
		spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);
		PSB_DEBUG_GENERAL("MSVDX: commit command to HW,seq=0x%08x\n",
				  sequence);
		ret = psb_msvdx_map_command(dev, cmd_buffer, cmd_offset,
					    cmd_size, NULL, sequence, 0);
		if (ret) {
			DRM_ERROR("MSVDXQUE: Failed to extract cmd\n");
			return ret;
		}
	} else {
		struct psb_msvdx_cmd_queue *msvdx_cmd;
		void *cmd = NULL;

		spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);
		/* queue the command to be sent when the h/w is ready */
		PSB_DEBUG_GENERAL("MSVDXQUE: queueing sequence:%08x..\n",
				  sequence);
		msvdx_cmd = kzalloc(sizeof(struct psb_msvdx_cmd_queue),
				    GFP_KERNEL);
		if (msvdx_cmd == NULL) {
			DRM_ERROR("MSVDXQUE: Out of memory...\n");
			return -ENOMEM;
		}

		ret = psb_msvdx_map_command(dev, cmd_buffer, cmd_offset,
					    cmd_size, &cmd, sequence, 1);
		if (ret) {
			DRM_ERROR("MSVDXQUE: Failed to extract cmd\n");
			kfree(msvdx_cmd
			     );
			return ret;
		}
		msvdx_cmd->cmd = cmd;
		msvdx_cmd->cmd_size = cmd_size;
		msvdx_cmd->sequence = sequence;
		spin_lock_irqsave(&msvdx_priv->msvdx_lock, irq_flags);
		list_add_tail(&msvdx_cmd->head, &msvdx_priv->msvdx_queue);
		spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);
		if (!msvdx_priv->msvdx_busy) {
			msvdx_priv->msvdx_busy = 1;
			PSB_DEBUG_GENERAL("MSVDXQUE: Need immediate dequeue\n");
			psb_msvdx_dequeue_send(dev);
		}
	}

	return ret;
}

int psb_cmdbuf_video(struct drm_file *priv,
		     struct list_head *validate_list,
		     uint32_t fence_type,
		     struct drm_psb_cmdbuf_arg *arg,
		     struct ttm_buffer_object *cmd_buffer,
		     struct psb_ttm_fence_rep *fence_arg)
{
	struct drm_device *dev = priv->minor->dev;
	struct ttm_fence_object *fence;
	int ret;

	/*
	 * Check this. Doesn't seem right. Have fencing done AFTER command
	 * submission and make sure drm_psb_idle idles the MSVDX completely.
	 */
	ret =
		psb_submit_video_cmdbuf(dev, cmd_buffer, arg->cmdbuf_offset,
					arg->cmdbuf_size, NULL);
	if (ret)
		return ret;


	/* DRM_ERROR("Intel: Fix video fencing!!\n"); */
	psb_fence_or_sync(priv, PSB_ENGINE_VIDEO, fence_type,
			  arg->fence_flags, validate_list, fence_arg,
			  &fence);

	ttm_fence_object_unref(&fence);
	spin_lock(&cmd_buffer->bdev->fence_lock);
	if (cmd_buffer->sync_obj != NULL)
		ttm_fence_sync_obj_unref(&cmd_buffer->sync_obj);
	spin_unlock(&cmd_buffer->bdev->fence_lock);

	return 0;
}


static int psb_msvdx_send(struct drm_device *dev, void *cmd,
			  unsigned long cmd_size)
{
	int ret = 0;
	struct drm_psb_private *dev_priv = dev->dev_private;

	while (cmd_size > 0) {
		uint32_t cur_cmd_size = MEMIO_READ_FIELD(cmd, FWRK_GENMSG_SIZE);
		uint32_t cur_cmd_id = MEMIO_READ_FIELD(cmd, FWRK_GENMSG_ID);
		if (cur_cmd_size > cmd_size) {
			ret = -EINVAL;
			DRM_ERROR("MSVDX:cmd_size %lu cur_cmd_size %lu\n",
				  cmd_size, (unsigned long)cur_cmd_size);
			goto out;
		}

		/* Send the message to h/w */
		ret = psb_mtx_send(dev_priv, cmd);
		if (ret) {
			PSB_DEBUG_GENERAL("MSVDX: ret:%d\n", ret);
			goto out;
		}
		cmd += cur_cmd_size;
		cmd_size -= cur_cmd_size;

	}

out:
	PSB_DEBUG_GENERAL("MSVDX: ret:%d\n", ret);
	return ret;
}

int psb_mtx_send(struct drm_psb_private *dev_priv, const void *msg)
{
	static uint32_t pad_msg[FWRK_PADMSG_SIZE];
	const uint32_t *p_msg = (uint32_t *) msg;
	uint32_t msg_num, words_free, ridx, widx, buf_size, buf_offset;
	int ret = 0;

	PSB_DEBUG_GENERAL("MSVDX: psb_mtx_send\n");

	/* we need clocks enabled before we touch VEC local ram */
	PSB_WMSVDX32(clk_enable_all, MSVDX_MAN_CLK_ENABLE);

	msg_num = (MEMIO_READ_FIELD(msg, FWRK_GENMSG_SIZE) + 3) / 4;

	/*
		{
			int i;
			printk("MSVDX: psb_mtx_send is %dDW\n", msg_num);

			for(i = 0; i < msg_num; i++)
				printk("0x%08x ", p_msg[i]);
			printk("\n");
		}
	*/
	buf_size = PSB_RMSVDX32(MSVDX_COMMS_TO_MTX_BUF_SIZE) & ((1 << 16) - 1);

	if (msg_num > buf_size) {
		ret = -EINVAL;
		DRM_ERROR("MSVDX: message exceed maximum,ret:%d\n", ret);
		goto out;
	}

	ridx = PSB_RMSVDX32(MSVDX_COMMS_TO_MTX_RD_INDEX);
	widx = PSB_RMSVDX32(MSVDX_COMMS_TO_MTX_WRT_INDEX);


	buf_size = PSB_RMSVDX32(MSVDX_COMMS_TO_MTX_BUF_SIZE) & ((1 << 16) - 1);
	/*0x2000 is VEC Local Ram offset*/
	buf_offset =
		(PSB_RMSVDX32(MSVDX_COMMS_TO_MTX_BUF_SIZE) >> 16) + 0x2000;

	/* message would wrap, need to send a pad message */
	if (widx + msg_num > buf_size) {
		/* Shouldn't happen for a PAD message itself */
		if (MEMIO_READ_FIELD(msg, FWRK_GENMSG_ID)
		       == FWRK_MSGID_PADDING)
			DRM_INFO("MSVDX WARNING: should not wrap pad msg, "
				"buf_size is %d, widx is %d, msg_num is %d.\n",
				buf_size, widx, msg_num);

		/* if the read pointer is at zero then we must wait for it to
		 * change otherwise the write pointer will equal the read
		 * pointer,which should only happen when the buffer is empty
		 *
		 * This will only happens if we try to overfill the queue,
		 * queue management should make
		 * sure this never happens in the first place.
		 */
		BUG_ON(0 == ridx);
		if (0 == ridx) {
			ret = -EINVAL;
			DRM_ERROR("MSVDX: RIndex=0, ret:%d\n", ret);
			goto out;
		}

		/* Send a pad message */
		MEMIO_WRITE_FIELD(pad_msg, FWRK_GENMSG_SIZE,
				  (buf_size - widx) << 2);
		MEMIO_WRITE_FIELD(pad_msg, FWRK_GENMSG_ID,
				  FWRK_MSGID_PADDING);
		psb_mtx_send(dev_priv, pad_msg);
		widx = PSB_RMSVDX32(MSVDX_COMMS_TO_MTX_WRT_INDEX);
	}

	if (widx >= ridx)
		words_free = buf_size - (widx - ridx) - 1;
	else
		words_free = ridx - widx - 1;

	BUG_ON(msg_num > words_free);
	if (msg_num > words_free) {
		ret = -EINVAL;
		DRM_ERROR("MSVDX: msg_num > words_free, ret:%d\n", ret);
		goto out;
	}
	while (msg_num > 0) {
		PSB_WMSVDX32(*p_msg++, buf_offset + (widx << 2));
		msg_num--;
		widx++;
		if (buf_size == widx)
			widx = 0;
	}

	PSB_WMSVDX32(widx, MSVDX_COMMS_TO_MTX_WRT_INDEX);

	/* Make sure clocks are enabled before we kick */
	PSB_WMSVDX32(clk_enable_all, MSVDX_MAN_CLK_ENABLE);

	/* signal an interrupt to let the mtx know there is a new message */
	/* PSB_WMSVDX32(1, MSVDX_MTX_KICKI); */
	PSB_WMSVDX32(1, MSVDX_MTX_KICK);

	/* Read MSVDX Register several times in case Idle signal assert */
	PSB_RMSVDX32(MSVDX_INTERRUPT_STATUS);
	PSB_RMSVDX32(MSVDX_INTERRUPT_STATUS);
	PSB_RMSVDX32(MSVDX_INTERRUPT_STATUS);
	PSB_RMSVDX32(MSVDX_INTERRUPT_STATUS);


out:
	return ret;
}

/*
 * MSVDX MTX interrupt
 */
static void psb_msvdx_mtx_interrupt(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *)dev->dev_private;
	static uint32_t buf[128]; /* message buffer */
	uint32_t ridx, widx, buf_size, buf_offset;
	uint32_t num, ofs; /* message num and offset */
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;
	int i;

	PSB_DEBUG_GENERAL("MSVDX:Got a MSVDX MTX interrupt\n");

	/* Are clocks enabled  - If not enable before
	 * attempting to read from VLR
	 */
	if (PSB_RMSVDX32(MSVDX_MAN_CLK_ENABLE) != (clk_enable_all)) {
		PSB_DEBUG_GENERAL("MSVDX:Clocks disabled when Interupt set\n");
		PSB_WMSVDX32(clk_enable_all, MSVDX_MAN_CLK_ENABLE);
	}


loop: /* just for coding style check */
	ridx = PSB_RMSVDX32(MSVDX_COMMS_TO_HOST_RD_INDEX);
	widx = PSB_RMSVDX32(MSVDX_COMMS_TO_HOST_WRT_INDEX);

	/* Get out of here if nothing */
	if (ridx == widx)
		goto done;

	buf_size = PSB_RMSVDX32(MSVDX_COMMS_TO_HOST_BUF_SIZE) & ((1 << 16) - 1);
	/*0x2000 is VEC Local Ram offset*/
	buf_offset =
		(PSB_RMSVDX32(MSVDX_COMMS_TO_HOST_BUF_SIZE) >> 16) + 0x2000;

	ofs = 0;
	buf[ofs] = PSB_RMSVDX32(buf_offset + (ridx << 2));

	/* round to nearest word */
	num = (MEMIO_READ_FIELD(buf, FWRK_GENMSG_SIZE) + 3) / 4;

	/* ASSERT(num <= sizeof(buf) / sizeof(uint32_t)); */

	if (++ridx >= buf_size)
		ridx = 0;

	for (ofs++; ofs < num; ofs++) {
		buf[ofs] = PSB_RMSVDX32(buf_offset + (ridx << 2));

		if (++ridx >= buf_size)
			ridx = 0;
	}

	/* Update the Read index */
	PSB_WMSVDX32(ridx, MSVDX_COMMS_TO_HOST_RD_INDEX);

	if (msvdx_priv->msvdx_needs_reset)
		goto loop;

	switch (MEMIO_READ_FIELD(buf, FWRK_GENMSG_ID)) {
	case VA_MSGID_CMD_HW_PANIC:
	case VA_MSGID_CMD_FAILED: {
		/* For VXD385 firmware, fence value is not validate here */
		uint32_t msg_id = MEMIO_READ_FIELD(buf, FWRK_GENMSG_ID);
		uint32_t diff = 0;
		uint32_t fence, fault;
		uint32_t last_mb, first_mb;

		if (msg_id == VA_MSGID_CMD_HW_PANIC)
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: get panic message.\n");
		else
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: get failed message.\n");

		if (msg_id == VA_MSGID_CMD_HW_PANIC) {
			fence = MEMIO_READ_FIELD(buf,
						 FW_VA_HW_PANIC_FENCE_VALUE);
			first_mb = MEMIO_READ_FIELD(buf,
						    FW_VA_HW_PANIC_FIRST_MB_NUM);
			last_mb = MEMIO_READ_FIELD(buf,
						   FW_VA_HW_PANIC_FAULT_MB_NUM);
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: PANIC MESSAGE fence is %d.\n", MEMIO_READ_FIELD(buf, FW_VA_HW_PANIC_FENCE_VALUE));
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: PANIC MESSAGE first mb num is %d.\n", MEMIO_READ_FIELD(buf, FW_VA_HW_PANIC_FIRST_MB_NUM));
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: PANIC MESSAGE fault mb num is %d.\n", MEMIO_READ_FIELD(buf, FW_VA_HW_PANIC_FAULT_MB_NUM));
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: PANIC MESSAGE fe status is 0x%x.\n", MEMIO_READ_FIELD(buf, FW_VA_HW_PANIC_FESTATUS));
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: PANIC MESSAGE be status is 0x%x.\n", MEMIO_READ_FIELD(buf, FW_VA_HW_PANIC_BESTATUS));
		} else {
			fence = MEMIO_READ_FIELD(buf,
						 FW_VA_CMD_FAILED_FENCE_VALUE);
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: FAILED MESSAGE fence is %d.\n", MEMIO_READ_FIELD(buf, FW_VA_HW_PANIC_FIRST_MB_NUM));
			PSB_DEBUG_MSVDX("MSVDX_DEBUG: FAILED MESSAGE flag is %d.\n", MEMIO_READ_FIELD(buf, FW_VA_CMD_FAILED_FLAGS));
		}

		if (IS_MDFLD(dev) && IS_FW_UPDATED) {
			fence = MEMIO_READ_FIELD(buf, FW_DEVA_CMD_FAILED_MSG_ID);
			fault = MEMIO_READ_FIELD(buf, FW_DEVA_CMD_FAILED_FLAGS);
		}

		if (msg_id == VA_MSGID_CMD_HW_PANIC)
			PSB_DEBUG_GENERAL("MSVDX: VA_MSGID_CMD_HW_PANIC:"
					  "Fault detected"
					  " - Fence: %08x"
					  " - resetting and ignoring error\n",
					  fence);
		else
			PSB_DEBUG_GENERAL("MSVDX: VA_MSGID_CMD_FAILED:"
					  "Fault detected"
					  " - Fence: %08x"
					  " - resetting and ignoring error\n",
					  fence);

		if (IS_D0(dev))
			msvdx_priv->msvdx_needs_reset |= MSVDX_RESET_NEEDS_REUPLOAD_FW |
				MSVDX_RESET_NEEDS_INIT_FW;
		else
			msvdx_priv->msvdx_needs_reset = 1;

		if (msg_id == VA_MSGID_CMD_HW_PANIC) {
			diff = msvdx_priv->msvdx_current_sequence
			       - dev_priv->sequence[PSB_ENGINE_VIDEO];

			if (diff > 0x0FFFFFFF)
				msvdx_priv->msvdx_current_sequence++;

			PSB_DEBUG_GENERAL("MSVDX: Fence ID missing, "
					  "assuming %08x\n",
					  msvdx_priv->msvdx_current_sequence);
		} else {
			msvdx_priv->msvdx_current_sequence = fence;
		}

		psb_fence_error(dev, PSB_ENGINE_VIDEO,
				msvdx_priv->msvdx_current_sequence,
				_PSB_FENCE_TYPE_EXE, DRM_CMD_FAILED);

		/* Flush the command queue */
		psb_msvdx_flush_cmd_queue(dev);

		msvdx_priv->fw_status = 1; /* set ERROR flag */

		goto done;
	}
	case VA_MSGID_CMD_COMPLETED: {
		uint32_t fence;
		uint32_t flags = MEMIO_READ_FIELD(buf, FW_VA_CMD_COMPLETED_FLAGS);
		/* uint32_t last_mb = MEMIO_READ_FIELD(buf,
		   FW_VA_CMD_COMPLETED_LASTMB);
		*/

		if (IS_MDFLD(dev) && IS_FW_UPDATED)
			fence = MEMIO_READ_FIELD(buf, FW_VA_CMD_COMPLETED_MSG_ID);
		else
			fence = MEMIO_READ_FIELD(buf, FW_VA_CMD_COMPLETED_FENCE_VALUE);

		PSB_DEBUG_GENERAL("MSVDX:VA_MSGID_CMD_COMPLETED: "
				  "FenceID: %08x, flags: 0x%x\n",
				  fence, flags);

		msvdx_priv->msvdx_current_sequence = fence;

		psb_fence_handler(dev, PSB_ENGINE_VIDEO);

		if (flags & FW_VA_RENDER_HOST_INT) {
			/*Now send the next command from the msvdx cmd queue */
			psb_msvdx_dequeue_send(dev);
			goto done;
		}

		break;
	}
	default:
		DRM_ERROR("ERROR: msvdx Unknown message from MTX, ID:0x%08x\n", MEMIO_READ_FIELD(buf, FWRK_GENMSG_ID));
		goto done;
	}

done:
	if (ridx != widx) {
		PSB_DEBUG_GENERAL("MSVDX Interrupt: there are more message to be read\n");
		goto loop;
	}

	if (IS_CTP(gpDrmDevice))
		drm_msvdx_pmpolicy = PSB_PMPOLICY_NOPM;

	/* we get a frame/slice done, try to save some power*/
	if (IS_D0(dev)) {
		if (drm_msvdx_pmpolicy == PSB_PMPOLICY_POWERDOWN)
			schedule_delayed_work(&dev_priv->scheduler.msvdx_suspend_wq, 0);
	} else {
		if (drm_msvdx_pmpolicy != PSB_PMPOLICY_NOPM)
			schedule_delayed_work(&dev_priv->scheduler.msvdx_suspend_wq, 0);
	}

	DRM_MEMORYBARRIER();	/* TBD check this... */
}


/*
 * MSVDX interrupt.
 */
IMG_BOOL psb_msvdx_interrupt(IMG_VOID *pvData)
{
	struct drm_device *dev;
	struct drm_psb_private *dev_priv;
	struct msvdx_private *msvdx_priv;
	uint32_t msvdx_stat;

	if (pvData == IMG_NULL) {
		DRM_ERROR("ERROR: msvdx %s, Invalid params\n", __func__);
		return IMG_FALSE;
	}

	dev = (struct drm_device *)pvData;

	dev_priv = (struct drm_psb_private *) dev->dev_private;
	msvdx_priv = dev_priv->msvdx_private;

	msvdx_priv->msvdx_hw_busy = REG_READ(0x20D0) & (0x1 << 9);

	msvdx_stat = PSB_RMSVDX32(MSVDX_INTERRUPT_STATUS);

	/* driver only needs to handle mtx irq
	 * For MMU fault irq, there's always a HW PANIC generated
	 * if HW/FW is totally hang, the lockup function will handle
	 * the reseting
	 */
	if (!IS_D0(dev) &&
	    (msvdx_stat & MSVDX_INTERRUPT_STATUS_CR_MMU_FAULT_IRQ_MASK)) {
		/*Ideally we should we should never get to this */
		PSB_DEBUG_IRQ("MSVDX:MMU Fault:0x%x\n", msvdx_stat);

		/* Pause MMU */
		PSB_WMSVDX32(MSVDX_MMU_CONTROL0_CR_MMU_PAUSE_MASK,
			     MSVDX_MMU_CONTROL0);
		DRM_WRITEMEMORYBARRIER();

		/* Clear this interupt bit only */
		PSB_WMSVDX32(MSVDX_INTERRUPT_STATUS_CR_MMU_FAULT_IRQ_MASK,
			     MSVDX_INTERRUPT_CLEAR);
		PSB_RMSVDX32(MSVDX_INTERRUPT_CLEAR);
		DRM_READMEMORYBARRIER();

		msvdx_priv->msvdx_needs_reset = 1;
	} else if (msvdx_stat & MSVDX_INTERRUPT_STATUS_CR_MTX_IRQ_MASK) {
		PSB_DEBUG_IRQ
			("MSVDX: msvdx_stat: 0x%x(MTX)\n", msvdx_stat);

		/* Clear all interupt bits */
		if (IS_D0(dev))
			PSB_WMSVDX32(MSVDX_INTERRUPT_STATUS_CR_MTX_IRQ_MASK,
				     MSVDX_INTERRUPT_CLEAR);
		else
			PSB_WMSVDX32(0xffff, MSVDX_INTERRUPT_CLEAR);

		PSB_RMSVDX32(MSVDX_INTERRUPT_CLEAR);
		DRM_READMEMORYBARRIER();

		psb_msvdx_mtx_interrupt(dev);
	}

	return IMG_TRUE;
}


void psb_msvdx_lockup(struct drm_psb_private *dev_priv,
		      int *msvdx_lockup, int *msvdx_idle)
{
	int diff;
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;

	*msvdx_lockup = 0;
	*msvdx_idle = 1;

#if 0
	PSB_DEBUG_GENERAL("MSVDXTimer: current_sequence:%d "
			  "last_sequence:%d and last_submitted_sequence :%d\n",
			  msvdx_priv->msvdx_current_sequence,
			  msvdx_priv->msvdx_last_sequence,
			  dev_priv->sequence[PSB_ENGINE_VIDEO]);
#endif

	diff = msvdx_priv->msvdx_current_sequence -
	       dev_priv->sequence[PSB_ENGINE_VIDEO];

	if (diff > 0x0FFFFFFF) {
		if (msvdx_priv->msvdx_current_sequence ==
		    msvdx_priv->msvdx_last_sequence) {
			DRM_ERROR("MSVDXTimer:locked-up for sequence:%d\n",
				  msvdx_priv->msvdx_current_sequence);
			*msvdx_lockup = 1;
		} else {
			PSB_DEBUG_GENERAL("MSVDXTimer: "
					  "msvdx responded fine so far\n");
			msvdx_priv->msvdx_last_sequence =
				msvdx_priv->msvdx_current_sequence;
			*msvdx_idle = 0;
		}
	}
}

int psb_check_msvdx_idle(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *)dev->dev_private;
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;
	/* drm_psb_msvdx_frame_info_t *current_frame = NULL; */

	if (msvdx_priv->msvdx_fw_loaded == 0)
		return 0;

	if (msvdx_priv->msvdx_busy) {
		PSB_DEBUG_PM("MSVDX: psb_check_msvdx_idle returns busy\n");
		return -EBUSY;
	}

	if (IS_D0(dev)) {
		PSB_DEBUG_MSVDX("   SIGNITURE is %x\n", PSB_RMSVDX32(MSVDX_COMMS_SIGNATURE));

		if (!(PSB_RMSVDX32(MSVDX_COMMS_FW_STATUS) & MSVDX_FW_STATUS_HW_IDLE))
			return -EBUSY;
	}
	/*
		if (msvdx_priv->msvdx_hw_busy) {
			PSB_DEBUG_PM("MSVDX: %s, HW is busy\n", __func__);
			return -EBUSY;
		}
	*/
	return 0;
}


int psb_remove_videoctx(struct drm_psb_private *dev_priv, struct file *filp)
{
	struct psb_video_ctx *pos, *n;
	/* iterate to query all ctx to if there is DRM running*/
	ied_enabled = 0;

	list_for_each_entry_safe(pos, n, &dev_priv->video_ctx, head) {
		if (pos->filp == filp) {
			PSB_DEBUG_GENERAL("Video:remove context profile %d,"
					  " entrypoint %d",
					  (pos->ctx_type >> 8) & 0xff,
					  (pos->ctx_type & 0xff));

			/* if current ctx points to it, set to NULL */
			if (dev_priv->topaz_ctx == pos) {
				/*Reset fw load status here.*/
				if (IS_MDFLD(dev_priv->dev) &&
					(VAEntrypointEncSlice ==
						(pos->ctx_type & 0xff)
					|| VAEntrypointEncPicture ==
						(pos->ctx_type & 0xff)))
					pnw_reset_fw_status(dev_priv->dev);

				dev_priv->topaz_ctx = NULL;
			} else if (IS_MDFLD(dev_priv->dev) &&
					(VAEntrypointEncSlice ==
						(pos->ctx_type & 0xff)
					|| VAEntrypointEncPicture ==
						(pos->ctx_type & 0xff)))
				PSB_DEBUG_GENERAL("Remove a inactive "\
						"encoding context.\n");

			if (dev_priv->last_topaz_ctx == pos)
				dev_priv->last_topaz_ctx = NULL;

			if (dev_priv->msvdx_ctx == pos)
				dev_priv->msvdx_ctx = NULL;
			if (dev_priv->last_msvdx_ctx == pos)
				dev_priv->last_msvdx_ctx = NULL;

			list_del(&pos->head);
			kfree(pos);
		} else {
			if (pos->ctx_type & VA_RT_FORMAT_PROTECTED)
				ied_enabled = 1;
		}
	}
	return 0;
}

static int psb_entrypoint_number(struct drm_psb_private *dev_priv,
		uint32_t entry_type)
{
	struct psb_video_ctx *pos, *n;
	int count = 0;

	entry_type &= 0xff;

	if (entry_type < VAEntrypointVLD ||
			entry_type > VAEntrypointEncPicture) {
		DRM_ERROR("Invalide entrypoint value %d.\n", entry_type);
		return -EINVAL;
	}

	list_for_each_entry_safe(pos, n, &dev_priv->video_ctx, head) {
		if (IS_MDFLD(dev_priv->dev) &&
				(entry_type == (pos->ctx_type & 0xff)))
			count++;

	}

	PSB_DEBUG_GENERAL("There are %d active entrypoint %d.\n",
			count, entry_type);
	return count;
}


int lnc_video_getparam(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_lnc_video_getparam_arg *arg = data;
	int ret = 0;
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *)file_priv->minor->dev->dev_private;
	uint32_t handle, i;
	uint32_t offset = 0;
	uint32_t device_info = 0;
	uint32_t ctx_type = 0;
	struct psb_video_ctx *video_ctx = NULL;
	uint32_t rar_ci_info[2];
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;

	switch (arg->key) {
	case LNC_VIDEO_GETPARAM_RAR_INFO:
		rar_ci_info[0] = dev_priv->rar_region_start;
		rar_ci_info[1] = dev_priv->rar_region_size;
		ret = copy_to_user((void __user *)((unsigned long)arg->value),
				   &rar_ci_info[0],
				   sizeof(rar_ci_info));
		break;
	case LNC_VIDEO_GETPARAM_CI_INFO:
		rar_ci_info[0] = dev_priv->ci_region_start;
		rar_ci_info[1] = dev_priv->ci_region_size;
		ret = copy_to_user((void __user *)((unsigned long)arg->value),
				   &rar_ci_info[0],
				   sizeof(rar_ci_info));
		break;
	case LNC_VIDEO_FRAME_SKIP:
		ret = -EFAULT;
		break;
	case LNC_VIDEO_DEVICE_INFO:
		device_info = 0xffff & dev_priv->video_device_fuse;
		device_info |= (0xffff & dev->pci_device) << 16;

		ret = copy_to_user((void __user *)((unsigned long)arg->value),
				   &device_info, sizeof(device_info));
		break;
	case IMG_VIDEO_NEW_CONTEXT:
		/* add video decode/encode context */
		ret = copy_from_user(&ctx_type, (void __user *)((unsigned long)arg->value),
				     sizeof(ctx_type));
		video_ctx = kmalloc(sizeof(struct psb_video_ctx), GFP_KERNEL);
		if (video_ctx == NULL) {
			ret = -ENOMEM;
			break;
		}
		INIT_LIST_HEAD(&video_ctx->head);
		video_ctx->ctx_type = ctx_type;
		video_ctx->filp = file_priv->filp;
		list_add(&video_ctx->head, &dev_priv->video_ctx);

		if (IS_MDFLD(dev_priv->dev) &&
				(VAEntrypointEncSlice ==
				 (ctx_type & 0xff)))
			pnw_reset_fw_status(dev_priv->dev);

		PSB_DEBUG_GENERAL("Video:add ctx profile %d, entry %d.\n",
					((ctx_type >> 8) & 0xff),
					(ctx_type & 0xff));
		PSB_DEBUG_GENERAL("Video:add context protected 0x%x.\n",
					(ctx_type & VA_RT_FORMAT_PROTECTED));
		if (ctx_type & VA_RT_FORMAT_PROTECTED)
			ied_enabled = 1;
		break;

	case IMG_VIDEO_RM_CONTEXT:
		psb_remove_videoctx(dev_priv, file_priv->filp);
		break;
	case IMG_VIDEO_DECODE_STATUS:
		ret = copy_to_user((void __user *)((unsigned long)arg->value),
					   &msvdx_priv->fw_status, sizeof(msvdx_priv->fw_status));
		break;

	case IMG_VIDEO_SET_DISPLAYING_FRAME:
		ret = copy_from_user(&msvdx_priv->displaying_frame,
				(void __user *)((unsigned long)arg->value),
				sizeof(msvdx_priv->displaying_frame));
		if (ret) {
			DRM_ERROR("IMG_VIDEO_SET_DISPLAYING_FRAME error.\n");
			return -EFAULT;
		}
		break;
	case IMG_VIDEO_GET_DISPLAYING_FRAME:
		ret = copy_to_user((void __user *)((unsigned long)arg->value),
				&msvdx_priv->displaying_frame,
				sizeof(msvdx_priv->displaying_frame));
		if (ret) {
			DRM_ERROR("IMG_VIDEO_GET_DISPLAYING_FRAME error.\n");
			return -EFAULT;
		}
		break;
	case IMG_DISPLAY_SET_WIDI_EXT_STATE:
		DRM_ERROR("variable drm_psb_widi has been removed\n");
		break;
	case IMG_VIDEO_GET_HDMI_STATE:
		ret = copy_to_user((void __user *)((unsigned long)arg->value),
				&hdmi_state,
				sizeof(hdmi_state));
		if (ret) {
			DRM_ERROR("IMG_VIDEO_GET_HDMI_STATE error.\n");
			return -EFAULT;
		}
		break;
	case IMG_VIDEO_SET_HDMI_STATE:
		if (!hdmi_state) {
			PSB_DEBUG_ENTRY(
				"wait 100ms for kernel hdmi pipe ready.\n");
			msleep(100);
		}
		if (dev_priv->bhdmiconnected)
			hdmi_state = (int)arg->value;
		else
			PSB_DEBUG_ENTRY(
				"skip hdmi_state setting, for unplugged.\n");

		PSB_DEBUG_ENTRY("%s, set hdmi_state = %d\n",
				 __func__, hdmi_state);
		break;
	case PNW_VIDEO_QUERY_ENTRY:
		ret = copy_from_user(&handle,
				(void __user *)((unsigned long)arg->arg),
				sizeof(handle));
		if (ret)
			break;
		/*Return the number of active entries*/
		i = psb_entrypoint_number(dev_priv, handle);
		if (i >= 0)
			ret = copy_to_user((void __user *)
					((unsigned long)arg->value),
					&i, sizeof(i));
		break;
		case IMG_VIDEO_IED_STATE:
		if (IS_MDFLD(dev)) {
			/* query IED status by register is not safe */
			/* need first power-on msvdx, while now only  */
			/* schedule vxd suspend wq in interrupt handler */
#if 0
			int ied_enable;
			/* VXD must be power on during query IED register */
			if (!ospm_power_using_hw_begin(OSPM_VIDEO_DEC_ISLAND,
					OSPM_UHB_FORCE_POWER_ON))
				return -EBUSY;
			/* wrong spec, IED should be located in pci device 2 */
			if (REG_READ(PSB_IED_DRM_CNTL_STATUS) & IED_DRM_VLD)
				ied_enable = 1;
			else
				ied_enable = 0;
			PSB_DEBUG_GENERAL("ied_enable is %d.\n", ied_enable);
			ospm_power_using_hw_end(OSPM_VIDEO_DEC_ISLAND);
#endif
			ret = copy_to_user((void __user *)
					((unsigned long)arg->value),
					&ied_enabled, sizeof(ied_enabled));
		} else { /* Moorestown should not call it */
			DRM_ERROR("IMG_VIDEO_IED_EANBLE error.\n");
			return -EFAULT;
		}
		break;

	default:
		ret = -EFAULT;
		break;
	}

	if (ret)
		return -EFAULT;

	return 0;
}

inline int psb_try_power_down_msvdx(struct drm_device *dev)
{
	ospm_apm_power_down_msvdx(dev);
	return 0;
}

int psb_msvdx_save_context(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
		(struct drm_psb_private *)dev->dev_private;
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;
	int offset = 0;

	if (IS_D0(dev))
		msvdx_priv->msvdx_needs_reset = MSVDX_RESET_NEEDS_INIT_FW;
	else
		msvdx_priv->msvdx_needs_reset = 1;

	for (offset = 0; offset < VEC_LOCAL_MEM_BYTE_SIZE / 4; ++offset)
		msvdx_priv->vec_local_mem_data[offset] =
			PSB_RMSVDX32(VEC_LOCAL_MEM_OFFSET + offset * 4);

	msvdx_priv->vec_local_mem_saved = 1;

	if (IS_D0(dev)) {
		PSB_WMSVDX32(0, MSVDX_MTX_ENABLE);
		psb_msvdx_reset(dev_priv);
		PSB_WMSVDX32(0, MSVDX_MAN_CLK_ENABLE);
	}

	return 0;
}

int psb_msvdx_restore_context(struct drm_device *dev)
{
	return 0;
}

int psb_msvdx_check_reset_fw(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct msvdx_private *msvdx_priv = dev_priv->msvdx_private;
	unsigned long irq_flags;

	spin_lock_irqsave(&msvdx_priv->msvdx_lock, irq_flags);

	/* handling fw upload here if required */
	/* power off first, then hw_begin will power up/upload FW correctly */
	if (msvdx_priv->msvdx_needs_reset & MSVDX_RESET_NEEDS_REUPLOAD_FW) {
		msvdx_priv->msvdx_needs_reset &= ~MSVDX_RESET_NEEDS_REUPLOAD_FW;
		ospm_power_island_down(OSPM_VIDEO_DEC_ISLAND);
	}
	spin_unlock_irqrestore(&msvdx_priv->msvdx_lock, irq_flags);
	return 0;
}

