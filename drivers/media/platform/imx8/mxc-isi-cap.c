/*
 * Copyright 2017-2018 NXP
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/of_graph.h>

#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-avt-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <soc/imx8/sc/sci.h>

#include "mxc-isi-core.h"
#include "mxc-isi-hw.h"
#include "mxc-media-dev.h"

enum flush_state {
	FLUSH_NOT_INITIATED = 0,
	FLUSH_IN_PROGRESS,
	FLUSH_DONE,
};

struct mxc_isi_fmt mxc_isi_out_formats[] = {
	{
		.name		= "SBGGR8",
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.depth		= { 8 },
		.color		= MXC_ISI_OUT_FMT_RAW8,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
	}, {
		.name		= "SGBGR",
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.depth		= { 8 },
		.color		= MXC_ISI_OUT_FMT_RAW8,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
	}, {
		.name		= "SGRGB8",
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.depth		= { 8 },
		.color		= MXC_ISI_OUT_FMT_RAW8,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
	}, {
		.name		= "SRGGB8",
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.depth		= { 8 },
		.color		= MXC_ISI_OUT_FMT_RAW8,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
	}, {
		.name		= "RGB565",
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= { 16 },
		.color		= MXC_ISI_OUT_FMT_RGB565,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code  = MEDIA_BUS_FMT_RGB565_1X16,
	}, {
		.name		= "RGB24",
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.depth		= { 24 },
		.color		= MXC_ISI_OUT_FMT_BGR32P,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code  = MEDIA_BUS_FMT_RGB888_1X24,
	}, {
		.name		= "BGR24",
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.depth		= { 24 },
		.color		= MXC_ISI_OUT_FMT_RGB32P,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code  = MEDIA_BUS_FMT_BGR888_1X24,
	}, {
		.name		= "ARGB32",
		.fourcc		= V4L2_PIX_FMT_ARGB32,
		.depth		= { 32 },
		.color		= MXC_ISI_OUT_FMT_ARGB32,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_ARGB8888_1X32,
	}, {
		.name		= "VYUY8",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= { 16 },
		.color		= MXC_ISI_OUT_FMT_YUV422_1P8P,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
	}, {
		.name		= "YUYV-16",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= { 16 },
		.color		= MXC_ISI_OUT_FMT_YUV422_1P8P,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_1X16,
	}, {
		.name		= "YUV32 (X-Y-U-V)",
		.fourcc		= V4L2_PIX_FMT_YUV32,
		.depth		= { 32 },
		.color		= MXC_ISI_OUT_FMT_YUV444_1P8,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= MEDIA_BUS_FMT_AYUV8_1X32,
	}, {
		.name		= "YUV444M (Y-U-V)",
		.fourcc		= V4L2_PIX_FMT_YUV444M,
		.depth		= { 8, 8, 8 },
		.color		= MXC_ISI_OUT_FMT_YUV444_3P8P,
		.memplanes	= 3,
		.colplanes	= 3,
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
	}, {
		.name		= "MONO8", /* Mono8 workaround */
		.fourcc		= V4L2_PIX_FMT_GREY,
		.depth		= { 8, 8 },
		.color		= MXC_ISI_OUT_FMT_YUV422_2P8P,
		.memplanes	= 2,
		.colplanes	= 2,
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
 	}
};

struct mxc_isi_fmt *mxc_isi_get_format(unsigned int index)
{
	return &mxc_isi_out_formats[index];
}

static void update_flush_state(struct mxc_isi_dev *mxc_isi,
	enum flush_state new_state)
{
	struct video_device *vdev = &mxc_isi->isi_cap.vdev;

	sprintf(&vdev->flush, "%d", new_state);
}

/**
 * mxc_isi_find_format - lookup mxc_isi color format by fourcc or media bus format
 */
struct mxc_isi_fmt *mxc_isi_find_format(const u32 *pixelformat,
						const u32 *mbus_code, int index)
{
	struct mxc_isi_fmt *fmt, *def_fmt = NULL;
	unsigned int i;
	int id = 0;

	if (index >= (int)ARRAY_SIZE(mxc_isi_out_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); i++) {
		fmt = &mxc_isi_out_formats[i];
		if (pixelformat && fmt->fourcc == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == id)
			def_fmt = fmt;
		id++;
	}
	return def_fmt;
}

struct mxc_isi_fmt *mxc_isi_get_src_fmt(struct v4l2_subdev_format *sd_fmt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); ++i)
		if (mxc_isi_out_formats[i].mbus_code == sd_fmt->format.code)
			return &mxc_isi_out_formats[i];

	return &mxc_isi_out_formats[0];
}

/*
 * mxc_isi_pipeline_enable() - Enable streaming on a pipeline
 *
 */
static int mxc_isi_pipeline_enable(struct mxc_isi_dev *mxc_isi, bool enable)
{
	struct media_entity *entity = &mxc_isi->isi_cap.vdev.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	struct media_graph graph;
	struct v4l2_subdev *subdev;
	int ret = 0;

	mutex_lock(&mdev->graph_mutex);

	ret = media_graph_walk_init(&graph, entity->graph_obj.mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		return ret;
	}
	media_graph_walk_start(&graph, entity);

	while ((entity = media_graph_walk_next(&graph))) {
		if (entity == NULL) {
			dev_dbg(&mxc_isi->pdev->dev,
					"%s ,entity is NULL\n", __func__);
			continue;
		}

		if (!is_media_entity_v4l2_subdev(entity)) {
			dev_dbg(&mxc_isi->pdev->dev,
					"%s ,entity is no v4l2, %s\n", __func__, entity->name);
			continue;
		}

		subdev = media_entity_to_v4l2_subdev(entity);
		if (subdev == NULL) {
			dev_dbg(&mxc_isi->pdev->dev,
					"%s ,%s,subdev is NULL\n", __func__, entity->name);
			continue;
		}

		ret = v4l2_subdev_call(subdev, video, s_stream, enable);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			dev_err(&mxc_isi->pdev->dev,
					"%s ,subdev %s s_stream failed\n", __func__, subdev->name);
			break;
		}
	}
	mutex_unlock(&mdev->graph_mutex);
	media_graph_walk_cleanup(&graph);

	return ret;
}

static int mxc_isi_update_buf_paddr(struct mxc_isi_buffer *buf, int memplanes)
{
	struct frame_addr *paddr = &buf->paddr;
	struct vb2_buffer *vb2 = &buf->v4l2_buf.vb2_buf;
	int ret = 0;

	/* support one plane now */
	paddr->cb = 0;
	paddr->cr = 0;

	switch (memplanes) {
	case 3:
		paddr->cr = vb2_dma_contig_plane_dma_addr(vb2, 2);
	case 2:
		paddr->cb = vb2_dma_contig_plane_dma_addr(vb2, 1);
	case 1:
		paddr->y = vb2_dma_contig_plane_dma_addr(vb2, 0);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

void mxc_isi_cap_frame_write_done(struct mxc_isi_dev *mxc_isi)
{
	struct mxc_isi_buffer *buf;
	struct vb2_buffer *vb2;

	if (list_empty(&mxc_isi->isi_cap.out_active)) {
		dev_warn(&mxc_isi->pdev->dev,
				"%s trying to access empty active list\n", __func__);
		return;
	}

	buf = list_first_entry(&mxc_isi->isi_cap.out_active,
				struct mxc_isi_buffer, list);

	if (buf->discard) {
		list_move_tail(mxc_isi->isi_cap.out_active.next,
					&mxc_isi->isi_cap.out_discard);
	} else {
		vb2 = &buf->v4l2_buf.vb2_buf;
		list_del_init(&buf->list);
		buf->v4l2_buf.vb2_buf.timestamp = ktime_get_ns();
		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, VB2_BUF_STATE_DONE);
		mxc_isi->isi_cap.frame_done_count++;
	}

	mxc_isi->isi_cap.frame_count++;

	if (list_empty(&mxc_isi->isi_cap.out_pending)) {
		if (list_empty(&mxc_isi->isi_cap.out_discard)) {
			dev_warn(&mxc_isi->pdev->dev,
					"%s: trying to access empty discard list\n", __func__);
			return;
		}

		buf = list_first_entry(&mxc_isi->isi_cap.out_discard,
					struct mxc_isi_buffer, list);
		buf->v4l2_buf.sequence = mxc_isi->isi_cap.frame_count;
		mxc_isi_channel_set_outbuf(mxc_isi, buf);
		list_move_tail(mxc_isi->isi_cap.out_discard.next,
					&mxc_isi->isi_cap.out_active);
		return;
	}

	/* ISI channel output buffer */
	buf = list_first_entry(&mxc_isi->isi_cap.out_pending,
					struct mxc_isi_buffer, list);

	buf->v4l2_buf.sequence = mxc_isi->isi_cap.frame_count;
	mxc_isi_channel_set_outbuf(mxc_isi, buf);
	vb2 = &buf->v4l2_buf.vb2_buf;
	vb2->state = VB2_BUF_STATE_ACTIVE;
	list_move_tail(mxc_isi->isi_cap.out_pending.next, &mxc_isi->isi_cap.out_active);
}

static int cap_vb2_queue_setup(struct vb2_queue *q,
		       unsigned int *num_buffers, unsigned int *num_planes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct mxc_isi_dev *mxc_isi = q->drv_priv;
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	struct mxc_isi_fmt *fmt = dst_f->fmt;
	unsigned long wh;
	int i;

	if (fmt == NULL)
		return -EINVAL;

	for (i = 0; i < fmt->memplanes; i++)
		alloc_devs[i] = &mxc_isi->pdev->dev;

	wh = dst_f->width * dst_f->height;

	*num_planes = fmt->memplanes;

	for (i = 0; i < fmt->memplanes; i++) {
		unsigned int size = (wh * fmt->depth[i]) / 8;

		if (i == 1 && fmt->fourcc == V4L2_PIX_FMT_NV12)
			size >>= 1;
		sizes[i] = max_t(u32, size, dst_f->sizeimage[i]);
	}
	dev_dbg(&mxc_isi->pdev->dev, "%s, buf_n=%d, size=%d\n",
					__func__,  *num_buffers, sizes[0]);

	return 0;
}

static int cap_vb2_buffer_prepare(struct vb2_buffer *vb2)
{
	struct vb2_queue *q = vb2->vb2_queue;
	struct mxc_isi_dev *mxc_isi = q->drv_priv;
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	int i;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	if (mxc_isi->isi_cap.dst_f.fmt == NULL)
		return -EINVAL;

	for (i = 0; i < dst_f->fmt->memplanes; i++) {
		unsigned long size = dst_f->sizeimage[i];

		if (vb2_plane_size(vb2, i) < size) {
			v4l2_err(&mxc_isi->isi_cap.vdev,
				 "User buffer too small (%ld < %ld)\n",
				 vb2_plane_size(vb2, i), size);
			return -EINVAL;
		}
#if 0 //debug only
		if (vb2_plane_vaddr(vb2, i))
			memset((void *)vb2_plane_vaddr(vb2, i), 0xaa,
					vb2_get_plane_payload(vb2, i));
#endif
		vb2_set_plane_payload(vb2, i, size);
	}

	return 0;
}

static void cap_vb2_buffer_queue(struct vb2_buffer *vb2)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb2);
	struct mxc_isi_buffer *buf
			= container_of(v4l2_buf, struct mxc_isi_buffer, v4l2_buf);
	struct mxc_isi_dev *mxc_isi = vb2_get_drv_priv(vb2->vb2_queue);
	unsigned long flags;

	/* Reset flush state, because new buffers are enqueued */
	update_flush_state(mxc_isi, FLUSH_NOT_INITIATED);

	spin_lock_irqsave(&mxc_isi->slock, flags);

	mxc_isi_update_buf_paddr(buf, mxc_isi->isi_cap.dst_f.fmt->mdataplanes);
	list_add_tail(&buf->list, &mxc_isi->isi_cap.out_pending);

	spin_unlock_irqrestore(&mxc_isi->slock, flags);
}

static int cap_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mxc_isi_dev *mxc_isi = q->drv_priv;
	struct mxc_isi_buffer *buf;
	struct vb2_buffer *vb2;
	unsigned long flags;
	int i, j;

    dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	if (count < 2)
		return -ENOBUFS;

	/* Create a buffer for discard operation */
	for (i = 0; i < mxc_isi->pix.num_planes; i++) {
		mxc_isi->discard_size[i] = mxc_isi->isi_cap.dst_f.sizeimage[i];
		mxc_isi->discard_buffer[i] = dma_alloc_coherent(&mxc_isi->pdev->dev,
					PAGE_ALIGN(mxc_isi->discard_size[i]),
					&mxc_isi->discard_buffer_dma[i], GFP_DMA | GFP_KERNEL);
		if (!mxc_isi->discard_buffer[i]) {
			for (j = 0; j < i; j++) {
				dma_free_coherent(&mxc_isi->pdev->dev,
							mxc_isi->discard_size[j],
							mxc_isi->discard_buffer[j],
							mxc_isi->discard_buffer_dma[j]);
				dev_err(&mxc_isi->pdev->dev, "%s: alloc dma buffer_%d fail\n",
							__func__, j);
			}
			return -ENOMEM;
		}
		dev_dbg(&mxc_isi->pdev->dev,
				"%s: num_plane=%d discard_size=%d discard_buffer=%p\n"
				, __func__, i,
				(int)mxc_isi->discard_size[i],
				mxc_isi->discard_buffer[i]);
	}

	spin_lock_irqsave(&mxc_isi->slock, flags);

	/* add two list member to out_discard list head */
	mxc_isi->buf_discard[0].discard = true;
	list_add_tail(&mxc_isi->buf_discard[0].list, &mxc_isi->isi_cap.out_discard);

	mxc_isi->buf_discard[1].discard = true;
	list_add_tail(&mxc_isi->buf_discard[1].list, &mxc_isi->isi_cap.out_discard);


	/* ISI channel output buffer 1 */
	buf = list_first_entry(&mxc_isi->isi_cap.out_discard,
					struct mxc_isi_buffer, list);
	buf->v4l2_buf.sequence = 0;
	vb2 = &buf->v4l2_buf.vb2_buf;
	vb2->state = VB2_BUF_STATE_ACTIVE;
	mxc_isi_channel_set_outbuf(mxc_isi, buf);
	list_move_tail(mxc_isi->isi_cap.out_discard.next, &mxc_isi->isi_cap.out_active);

	/* ISI channel output buffer 2 */
	buf = list_first_entry(&mxc_isi->isi_cap.out_pending,
					struct mxc_isi_buffer, list);
	buf->v4l2_buf.sequence = 1;
	vb2 = &buf->v4l2_buf.vb2_buf;
	vb2->state = VB2_BUF_STATE_ACTIVE;
	mxc_isi_channel_set_outbuf(mxc_isi, buf);
	list_move_tail(mxc_isi->isi_cap.out_pending.next, &mxc_isi->isi_cap.out_active);

	/* Clear frame count */
	mxc_isi->isi_cap.frame_count = 1;
	spin_unlock_irqrestore(&mxc_isi->slock, flags);

	return 0;
}

static void cap_vb2_stop_streaming(struct vb2_queue *q)
{
	struct mxc_isi_dev *mxc_isi = q->drv_priv;
	struct mxc_isi_buffer *buf, *tmp;
	unsigned long flags;
	int i;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	mxc_isi_channel_disable(mxc_isi);

	spin_lock_irqsave(&mxc_isi->slock, flags);

	while (!list_empty(&mxc_isi->isi_cap.out_active)) {
		buf = list_entry(mxc_isi->isi_cap.out_active.next, struct mxc_isi_buffer, list);

		list_del(&buf->list);
		if (buf->discard)
			continue;

		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	while (!list_empty(&mxc_isi->isi_cap.out_pending)) {
		buf = list_entry(mxc_isi->isi_cap.out_pending.next, struct mxc_isi_buffer, list);

		list_del(&buf->list);
		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	while (!list_empty(&mxc_isi->isi_cap.out_discard)) {
		buf = list_entry(mxc_isi->isi_cap.out_discard.next, struct mxc_isi_buffer, list);
		list_del(&buf->list);
	}

	list_for_each_entry_safe(buf, tmp,
				&mxc_isi->isi_cap.out_active, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	list_for_each_entry_safe(buf, tmp,
				&mxc_isi->isi_cap.out_pending, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	INIT_LIST_HEAD(&mxc_isi->isi_cap.out_active);
	INIT_LIST_HEAD(&mxc_isi->isi_cap.out_pending);
	INIT_LIST_HEAD(&mxc_isi->isi_cap.out_discard);

	spin_unlock_irqrestore(&mxc_isi->slock, flags);

	for (i = 0; i < mxc_isi->pix.num_planes; i++)
		dma_free_coherent(&mxc_isi->pdev->dev,
					mxc_isi->discard_size[i],
					mxc_isi->discard_buffer[i],
					mxc_isi->discard_buffer_dma[i]);
}

static struct vb2_ops mxc_cap_vb2_qops = {
	.queue_setup		= cap_vb2_queue_setup,
	.buf_prepare		= cap_vb2_buffer_prepare,
	.buf_queue			= cap_vb2_buffer_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= cap_vb2_start_streaming,
	.stop_streaming		= cap_vb2_stop_streaming,
};

/*
 * V4L2 controls handling
 */
#define ctrl_to_mxc_isi(__ctrl) \
	container_of((__ctrl)->handler, struct mxc_isi_dev, ctrls.handler)

static int mxc_isi_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mxc_isi_dev *mxc_isi = ctrl_to_mxc_isi(ctrl);
	unsigned long flags;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	spin_lock_irqsave(&mxc_isi->slock, flags);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctrl->val < 0)
			return -EINVAL;
		mxc_isi->hflip = (ctrl->val > 0) ? 1 : 0;
		break;

	case V4L2_CID_VFLIP:
		if (ctrl->val < 0)
			return -EINVAL;
		mxc_isi->vflip = (ctrl->val > 0) ? 1 : 0;
		break;

	case V4L2_CID_ALPHA_COMPONENT:
		if (ctrl->val < 0 || ctrl->val > 255)
			return -EINVAL;
		mxc_isi->alpha = ctrl->val;
		mxc_isi->alphaen = 1;
		break;

	default:
		dev_err(&mxc_isi->pdev->dev, "%s: Not support %d CID\n", __func__, ctrl->id);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&mxc_isi->slock, flags);

	return 0;
}

static const struct v4l2_ctrl_ops mxc_isi_ctrl_ops = {
	.s_ctrl = mxc_isi_s_ctrl,
};

int mxc_isi_ctrls_create(struct mxc_isi_dev *mxc_isi)
{
	struct mxc_isi_ctrls *ctrls = &mxc_isi->ctrls;
	struct v4l2_ctrl_handler *handler = &ctrls->handler;

	if (mxc_isi->ctrls.ready)
		return 0;

	v4l2_ctrl_handler_init(handler, 4);

/* disable isi controls */
/*
	ctrls->hflip = v4l2_ctrl_new_std(handler, &mxc_isi_ctrl_ops,
					V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctrls->vflip = v4l2_ctrl_new_std(handler, &mxc_isi_ctrl_ops,
					V4L2_CID_VFLIP, 0, 1, 1, 0);

	ctrls->alpha = v4l2_ctrl_new_std(handler, &mxc_isi_ctrl_ops,
					V4L2_CID_ALPHA_COMPONENT, 0, 0xff, 1, 0);
*/
	if (!handler->error)
		ctrls->ready = true;

	return handler->error;
}

void mxc_isi_ctrls_delete(struct mxc_isi_dev *mxc_isi)
{
	struct mxc_isi_ctrls *ctrls = &mxc_isi->ctrls;

	if (ctrls->ready) {
		v4l2_ctrl_handler_free(&ctrls->handler);
		ctrls->ready = false;
		ctrls->alpha = NULL;
	}
}

static struct media_pad *mxc_isi_get_remote_source_pad(struct mxc_isi_dev *mxc_isi)
{
	struct mxc_isi_cap_dev *isi_cap = &mxc_isi->isi_cap;
	struct v4l2_subdev *subdev = &isi_cap->sd;
	struct media_pad *sink_pad, *source_pad;
	int i;

	while (1) {
		source_pad = NULL;
		for (i = 0; i < subdev->entity.num_pads; i++) {
			sink_pad = &subdev->entity.pads[i];

			if (sink_pad->flags & MEDIA_PAD_FL_SINK) {
				source_pad = media_entity_remote_pad(sink_pad);
				if (source_pad)
					break;
			}
		}
		/* return first pad point in the loop  */
		return source_pad;
	}

	if (i == subdev->entity.num_pads)
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);

	return NULL;
}

static int mxc_isi_capture_open(struct file *file)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct media_pad *source_pad;
	struct v4l2_subdev *sd;
	struct device *dev = &mxc_isi->pdev->dev;
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	struct v4l2_subdev_format sd_fmt = {};
	struct v4l2_fh *fh;
	struct v4l2_subdev_fh* subdev_fh;
	int ret = -EBUSY;
	int i;

	dev_dbg(&mxc_isi->pdev->dev, "%s, ISI%d\n", __func__, mxc_isi->id);

	if (mxc_isi->is_m2m) {
		v4l2_err(mxc_isi->v4l2_dev, "%s: ISI channel[%d] is busy\n",
					__func__, mxc_isi->id);
		return -EBUSY;
	}
	atomic_inc(&mxc_isi->open_count);
	mxc_isi->is_m2m = 0;

	/* Get remote source pad */
	source_pad = mxc_isi_get_remote_source_pad(mxc_isi);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		ret = -EINVAL;
		goto fail;
	}

	/* Get remote source pad subdev */
	sd = media_entity_to_v4l2_subdev(source_pad->entity);
	if (sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		ret = -EINVAL;
		goto fail;
	}

	mutex_lock(&mxc_isi->lock);
	ret = v4l2_fh_open(file);
	mutex_unlock(&mxc_isi->lock);

	pm_runtime_get_sync(dev);

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret) {
		/* make this optional */
		v4l2_warn(mxc_isi->v4l2_dev, "%s, Call subdev s_power fail!\n", __func__);
	}

	fh = file->private_data;
	subdev_fh = to_v4l2_subdev_fh(fh);

	if (sd->internal_ops && sd->internal_ops->open) {
		ret = sd->internal_ops->open(sd, subdev_fh);
		if (ret < 0)
			goto fail;
	}

	/* Get the default resolution from subdev here */
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	dst_f->width = sd_fmt.format.width;
	dst_f->height = sd_fmt.format.height;
	set_frame_bounds(dst_f, sd_fmt.format.width, sd_fmt.format.height);

	if(!dst_f->fmt)
	{
		/* Check if we support current format from the subdev and then set the format */
		for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); ++i) {
			if (mxc_isi_out_formats[i].mbus_code == sd_fmt.format.code) {
				dst_f->fmt = &mxc_isi_out_formats[i];
			}
		}
	}

	/* Check if the format is already set */
	if (dst_f->fmt == NULL) {
		/* make this optional */
		v4l2_warn(mxc_isi->v4l2_dev, "%s, Destination default format is not set!!\n", __func__);
	}

	return 0;

fail:
	atomic_dec(&mxc_isi->open_count);
	return ret;
}

static int mxc_isi_capture_release(struct file *file)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct media_pad *source_pad;
	struct v4l2_subdev *sd;
	struct device *dev = &mxc_isi->pdev->dev;
	struct v4l2_fh *fh;
	struct v4l2_subdev_fh* subdev_fh;
	int ret;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	/* Get remote source pad */
	source_pad = mxc_isi_get_remote_source_pad(mxc_isi);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		ret = -EINVAL;
		goto label;
	}

	/* Get remote source pad subdev */
	sd = media_entity_to_v4l2_subdev(source_pad->entity);
	if (sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		ret = -EINVAL;
		goto label;
	}

	mutex_lock(&mxc_isi->lock);
	ret = _vb2_fop_release(file, NULL);
	if (ret) {
		v4l2_err(mxc_isi->v4l2_dev, "%s fail\n", __func__);
		mutex_unlock(&mxc_isi->lock);
		goto label;
	}
	mutex_unlock(&mxc_isi->lock);

	fh = file->private_data;
	subdev_fh = to_v4l2_subdev_fh(fh);

	if (sd->internal_ops && sd->internal_ops->close) {
		ret = sd->internal_ops->close(sd, subdev_fh);
		if (ret < 0)
			goto label;
	}

	if (atomic_read(&mxc_isi->open_count) > 0 &&
		atomic_dec_and_test(&mxc_isi->open_count))
		mxc_isi_channel_deinit(mxc_isi);

	ret = v4l2_subdev_call(sd, core, s_power, 0);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		v4l2_err(mxc_isi->v4l2_dev, "%s s_power fail\n", __func__);
		goto label;
	}

label:
	pm_runtime_put(dev);
	return (ret) ? ret : 0;
}

static const struct v4l2_file_operations mxc_isi_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= mxc_isi_capture_open,
	.release	= mxc_isi_capture_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

/*
 * Format and crop negotiation helpers
 */

/*
 * The video node ioctl operations
 */
static int mxc_isi_cap_querycap(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);

	strlcpy(cap->driver, MXC_ISI_DRIVER_NAME, sizeof(cap->driver));
	strlcpy(cap->card, MXC_ISI_DRIVER_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s.%d",
		 dev_name(&mxc_isi->pdev->dev), mxc_isi->id);

	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE_MPLANE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static struct v4l2_subdev *mxc_isi_get_sensor_subdev(struct v4l2_subdev *subdev);

static int mxc_isi_cap_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct v4l2_subdev_mbus_code_enum code;
	struct mxc_isi_fmt *fmt;

	int i, j, ret;
	struct mxc_isi_fmt* out_fmts[ARRAY_SIZE(mxc_isi_out_formats)];

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);

	for (i = j = 0; i < ARRAY_SIZE(mxc_isi_out_formats); ++i) {
		ret = 0;
		code.index = 0;

		while (ret == 0) {
			ret = v4l2_subdev_call(sd, pad, enum_mbus_code, NULL, &code);
			if (mxc_isi_out_formats[i].mbus_code == code.code) {
				out_fmts[j++] = &mxc_isi_out_formats[i];
				break;
			}
			code.index++;
		}
	}

	if (f->index >= j)
		return -EINVAL;


	fmt = out_fmts[f->index];
	if (!fmt)
		return -EINVAL;

	strncpy(f->description, fmt->name, sizeof(f->description) - 1);

	f->pixelformat = fmt->fourcc;

	return 0;
}

static void mxc_isi_cap_try_to_set_default_formats(struct mxc_isi_dev *mxc_isi)
{
	struct v4l2_subdev *sd;
	struct v4l2_subdev_mbus_code_enum code;
	struct v4l2_subdev_format sd_fmt = {};
	int ret, i;

	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);

	/* Get the default format from the subdev */
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sd_fmt);

	/* Check if we suuport current format from the subdev */
	for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); ++i) {
		if (mxc_isi_out_formats[i].mbus_code == sd_fmt.format.code) {
			mxc_isi->isi_cap.dst_f.fmt = &mxc_isi_out_formats[i];
			mxc_isi->isi_cap.src_f.fmt = &mxc_isi_out_formats[i];
			return;
		}
	}

	/* Check if the format is already set, if not,
	 * query the subdev for available formats
	 */
	if (mxc_isi->isi_cap.dst_f.fmt == NULL || ret) {
		for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); ++i) {
			ret = 0;
			code.index = 0;

			while (ret == 0) {
				ret = v4l2_subdev_call(sd, pad, enum_mbus_code, NULL, &code);
				if (mxc_isi_out_formats[i].mbus_code == code.code) {
					mxc_isi->isi_cap.dst_f.fmt = &mxc_isi_out_formats[i];
					mxc_isi->isi_cap.src_f.fmt = &mxc_isi_out_formats[i];
					return;
				}
				code.index++;
			}
		}
	}
}

static void update_bytesperline_and_sizeimage(struct mxc_isi_frame *dst_f, struct v4l2_pix_format_mplane *pix)
{
	struct mxc_isi_fmt *fmt = dst_f->fmt;
	int bpl, i;

	for (i = 0; i < pix->num_planes; i++) {
		bpl = pix->plane_fmt[i].bytesperline;

		if ((bpl == 0) || (bpl / (fmt->depth[i] >> 3)) < pix->width)
			pix->plane_fmt[i].bytesperline =
						(pix->width * fmt->depth[i]) >> 3;

		if (pix->plane_fmt[i].sizeimage == 0) {

			if ((i == 1) && (pix->pixelformat == V4L2_PIX_FMT_NV12))
				pix->plane_fmt[i].sizeimage =
					(pix->width * (pix->height >> 1) * fmt->depth[i] >> 3);
			else
				pix->plane_fmt[i].sizeimage = (pix->width * pix->height *
						fmt->depth[i] >> 3);
		}
	}

	if (pix->num_planes > 1) {
		for (i = 0; i < pix->num_planes; i++) {
			dst_f->bytesperline[i] = pix->plane_fmt[i].bytesperline;
			dst_f->sizeimage[i] = pix->plane_fmt[i].sizeimage;
		}
	} else {
		dst_f->bytesperline[0] = dst_f->width * dst_f->fmt->depth[0] / 8;
		dst_f->sizeimage[0] = dst_f->height * dst_f->bytesperline[0];
	}

	/* Mono8 workaround */
 	if(pix->pixelformat == V4L2_PIX_FMT_GREY)
	{
		pix->num_planes = 1;
	}
}


static int mxc_isi_cap_g_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct mxc_isi_frame *dst_f;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	/* Check if the format is already set, if not, query the subdev */
	if (mxc_isi->isi_cap.dst_f.fmt == NULL) {
		mxc_isi_cap_try_to_set_default_formats(mxc_isi);
	}

	dst_f = &mxc_isi->isi_cap.dst_f;

	pix->width = dst_f->o_width;
	pix->height = dst_f->o_height;
	pix->field = V4L2_FIELD_NONE;
	pix->pixelformat = dst_f->fmt->fourcc;
	pix->colorspace = V4L2_COLORSPACE_JPEG;
	pix->num_planes = dst_f->fmt->memplanes;

	update_bytesperline_and_sizeimage(dst_f, pix);

	return 0;
}


static int mxc_isi_cap_try_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct mxc_isi_fmt *fmt;
	int i;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); i++) {
		fmt = &mxc_isi_out_formats[i];
		if (fmt->fourcc == pix->pixelformat)
			break;
	}
	if (i >= ARRAY_SIZE(mxc_isi_out_formats)) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, format is not support!\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static struct media_pad *subdev_get_remote_source_pad(struct v4l2_subdev *subdev)
{
	struct media_pad *sink_pad, *source_pad;
	int i;

	while (1) {
		source_pad = NULL;
		for (i = 0; i < subdev->entity.num_pads; i++) {
			sink_pad = &subdev->entity.pads[i];

			if (sink_pad->flags & MEDIA_PAD_FL_SINK) {
				source_pad = media_entity_remote_pad(sink_pad);
				if (source_pad)
					break;
			}
		}
		/* return first pad point in the loop  */
		return source_pad;
	}

	return NULL;
}

static struct v4l2_subdev *mxc_isi_get_sensor_subdev(struct v4l2_subdev *subdev)
{
	struct media_pad *source_pad;
	struct v4l2_subdev *sd;

	/* Firstly find mipi interface, so remote source for the isi */
	source_pad = subdev_get_remote_source_pad(subdev);
	if (source_pad == NULL) {
		v4l2_err(subdev->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return NULL;
	}

	/* Get remote source pad subdev */
	sd = media_entity_to_v4l2_subdev(source_pad->entity);
	if (sd == NULL) {
		v4l2_err(subdev->v4l2_dev, "Can't find subdev\n");
		return NULL;
	}

	/* Then find the actual sensor which should be on the pad of mipi */
	source_pad = subdev_get_remote_source_pad(sd);
	if (source_pad == NULL) {
		v4l2_err(subdev->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return NULL;
	}

	/* Get remote source pad subdev */
	sd = media_entity_to_v4l2_subdev(source_pad->entity);
	if (sd == NULL) {
		v4l2_err(subdev->v4l2_dev, "Can't find subdev\n");
		return NULL;
	}

	return sd;
}

/* Update input frame size and format */
static int mxc_isi_source_fmt_init(struct mxc_isi_dev *mxc_isi)
{
	struct mxc_isi_frame *src_f = &mxc_isi->isi_cap.src_f;
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	struct v4l2_subdev_format src_fmt = {};
	struct media_pad *source_pad;
	struct v4l2_subdev *src_sd;
	int ret;

	/* Get remote source pad */
	source_pad = subdev_get_remote_source_pad(&mxc_isi->isi_cap.sd);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	src_sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (src_sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	src_fmt.pad = source_pad->index;
	src_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	src_fmt.format.width = dst_f->width;
	src_fmt.format.height = dst_f->height;
	src_fmt.format.code = dst_f->fmt->mbus_code;
	ret = v4l2_subdev_call(src_sd, pad, set_fmt, NULL, &src_fmt);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, set remote fmt fail!\n", __func__);
		return -EINVAL;
	}

	/* Pixel link master will transfer format to RGB32 or YUV32 */
	src_f->fmt = mxc_isi_get_src_fmt(&src_fmt);

	set_frame_bounds(src_f, src_fmt.format.width, src_fmt.format.height);
	set_frame_bounds(dst_f, src_fmt.format.width, src_fmt.format.height);

	if (dst_f->width > src_f->width || dst_f->height > src_f->height) {
		dev_err(&mxc_isi->pdev->dev,
				"%s: src:(%d,%d), dst:(%d,%d) Not support upscale\n", __func__,
				src_f->width, src_f->height,
				dst_f->width, dst_f->height);
		return -EINVAL;
	}

	return 0;
}

static int mxc_isi_cap_s_fmt_mplane(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	struct mxc_isi_fmt *fmt;
	struct v4l2_subdev_format sd_fmt = {};
	struct v4l2_subdev *sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	int ret;
	int i;


	/* Step1: Check format with output support format list.
	 * Step2: Update output frame information.
	 * Step3: Checkout the format whether is supported by remote subdev
	 *	 Step3.1: If Yes, call remote subdev set_fmt.
	 *	 Step3.2: If NO, call remote subdev get_fmt.
	 * Step4: Update input frame information.
	 * Step5: Update mxc isi channel configuration.
	 * */

	dev_dbg(&mxc_isi->pdev->dev, "%s, fmt=0x%X\n", __func__, pix->pixelformat);
	if (vb2_is_busy(&mxc_isi->isi_cap.vb2_q)) {
		return -EBUSY;
	}

	/* Check out put format */
	for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); i++) {
		fmt = &mxc_isi_out_formats[i];
		if (pix && fmt->fourcc == pix->pixelformat)
			break;
	}

	if (i >= ARRAY_SIZE(mxc_isi_out_formats)) {
		dev_dbg(&mxc_isi->pdev->dev, "%s, format is not support!\n", __func__);
		return -EINVAL;
	}

	/* update out put frame size and formate */
	if (pix->height <= 0 || pix->width <= 0) {

		if (sd == NULL)
			return -EINVAL;

		/* Get the default resolution from subdev here */
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sd_fmt);
		if (ret)
			return ret;

		pix->width = sd_fmt.format.width;
		pix->height = sd_fmt.format.height;
	}

	sd_fmt.which = V4L2_SUBDEV_FORMAT_TRY;
	sd_fmt.format.width = pix->width;
	sd_fmt.format.height = pix->height;
	ret = v4l2_subdev_call(sd, pad, set_fmt, NULL, &sd_fmt);
	if (ret != 0) {
		return ret;
	}

	dst_f->fmt = fmt;
	dst_f->height = pix->height;
	dst_f->width = pix->width;

	pix->num_planes = fmt->memplanes;

	update_bytesperline_and_sizeimage(dst_f, pix);

	memcpy(&mxc_isi->pix, pix, sizeof(*pix));

	set_frame_bounds(dst_f, pix->width, pix->height);

	/* Update fmt */
	ret = mxc_isi_source_fmt_init(mxc_isi);
	if (ret < 0)
		return -EINVAL;

	return 0;
}

static int mxc_isi_config_parm(struct mxc_isi_dev *mxc_isi)
{
	int ret;

	ret = mxc_isi_source_fmt_init(mxc_isi);
	if (ret < 0)
		return -EINVAL;

	mxc_isi_channel_init(mxc_isi);
	mxc_isi_channel_config(mxc_isi);

	return 0;
}

static int mxc_isi_cap_streamon(struct file *file, void *priv,
			     enum v4l2_buf_type type)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	int ret;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	ret = mxc_isi_config_parm(mxc_isi);
	if (ret < 0)
		return -EINVAL;

	ret = vb2_ioctl_streamon(file, priv, type);
	mxc_isi_channel_enable(mxc_isi);
	mxc_isi_pipeline_enable(mxc_isi, 1);

	mxc_isi->is_streaming = 1;

	return ret;
}

static int mxc_isi_cap_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	int ret;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	mxc_isi_pipeline_enable(mxc_isi, 0);
	mxc_isi_channel_disable(mxc_isi);
	ret = vb2_ioctl_streamoff(file, priv, type);

	mxc_isi->is_streaming = 0;

	return ret;
}

static int mxc_isi_cap_g_selection(struct file *file, void *fh,
				struct v4l2_selection *s)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct v4l2_subdev_selection ss;
	int retval;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	if (!v4l2_subdev_has_op(sd, pad, get_selection))
		return -ENOTTY;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ss.target = s->target;
	ss.flags = s->flags;
	memcpy(&ss.r, &s->r, sizeof(struct v4l2_rect));

	retval = v4l2_subdev_call(sd, pad, get_selection, NULL, &ss);

	s->target = ss.target;
	s->flags = ss.flags;
	memcpy(&s->r, &ss.r, sizeof(struct v4l2_rect));

	return retval;
}

static int mxc_isi_cap_s_selection(struct file *file, void *fh,
				struct v4l2_selection *s)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	struct mxc_isi_frame *src_f = &mxc_isi->isi_cap.src_f;
	struct v4l2_subdev *sd;
	struct v4l2_subdev_selection ss;
	int ret = 0;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	if (!v4l2_subdev_has_op(sd, pad, set_selection))
		return -ENOTTY;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!v4l2_subdev_has_op(sd, pad, set_selection))
		return -ENOTTY;

	ss.target = s->target;
	ss.flags = s->flags;
	memcpy(&ss.r, &s->r, sizeof(struct v4l2_rect));

	ret = v4l2_subdev_call(sd, pad, set_selection, NULL, &ss);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, set remote crop fail!\n", __func__);
		return -EINVAL;
	}

	src_f->width = dst_f->width = ss.r.width;
	src_f->height = dst_f->height = ss.r.height;

	/* Update fmt */
	ret = mxc_isi_source_fmt_init(mxc_isi);
	if (ret < 0)
		return -EINVAL;

	return 0;

}

static int mxc_isi_cap_g_chip_ident(struct file *file, void *fb,
			struct v4l2_dbg_chip_ident *chip)
{
	struct device_node *local, *remote, *endpoint;
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	struct v4l2_dbg_chip_ident ci;
	struct v4l2_subdev *sd;
	struct media_pad *source_pad;

	source_pad = mxc_isi_get_remote_source_pad(mxc_isi);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}

	local = dev_of_node(sd->dev);
	if (!local) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, Get device node fail\n", __func__);
		return -ENODEV;
	}

	endpoint = of_graph_get_endpoint_by_regs(local, -1, -1);
	if (!endpoint) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No %s endpoint\n",
						__func__, local->name);
		return -ENODEV;
	}

	remote = of_graph_get_remote_port_parent(endpoint);
	if (!remote) {
		v4l2_err(mxc_isi->v4l2_dev, "%s No remote port for %s\n",
					__func__, endpoint->name);
		return -ENODEV;
	}

	sprintf(chip->match.name, "%s-%d\n", sd->name, vdev->num);

	/* Just check if the callback of the sensor device returns success,
	 * no need to actually identify the device since we're using the
	 * pads to find it.
	 */

	return v4l2_subdev_call(sd, core, g_chip_ident, &ci);
}

static int mxc_isi_cap_g_parm(struct file *file, void *fh,
			struct v4l2_streamparm *a)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct media_pad *source_pad;

	source_pad = mxc_isi_get_remote_source_pad(mxc_isi);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}
	return v4l2_subdev_call(sd, video, g_parm, a);
}

static int mxc_isi_cap_s_parm(struct file *file, void *fh,
			struct v4l2_streamparm *a)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct media_pad *source_pad;

	source_pad = mxc_isi_get_remote_source_pad(mxc_isi);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (sd == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote subdev found!\n", __func__);
		return -EINVAL;
	}
	return v4l2_subdev_call(sd, video, s_parm, a);
}

static int mxc_isi_cap_enum_framesizes(struct file *file, void *priv,
					 struct v4l2_frmsizeenum *fsize)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct mxc_isi_fmt *fmt;
	struct media_pad *source_pad;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmt = mxc_isi_find_format(&fsize->pixel_format, NULL, 0);
	if (!fmt || fmt->fourcc != fsize->pixel_format)
		return -EINVAL;
	fse.code = fmt->mbus_code;

	source_pad = mxc_isi_get_remote_source_pad(mxc_isi);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (sd == NULL) {
		v4l2_err(&mxc_isi->isi_cap.sd, "Can't find subdev\n");
		return -ENODEV;
	}

	ret = v4l2_subdev_call(sd, pad, enum_frame_size, NULL, &fse);
	if (ret)
		return ret;

	if (fse.min_width == fse.max_width &&
	    fse.min_height == fse.max_height) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = fse.min_width;
		fsize->discrete.height = fse.min_height;
		return 0;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = fse.min_width;
	fsize->stepwise.max_width = fse.max_width;
	fsize->stepwise.min_height = fse.min_height;
	fsize->stepwise.max_height = fse.max_height;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int mxc_isi_cap_enum_frameintervals(struct file *file, void *fh,
					  struct v4l2_frmivalenum *interval)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct mxc_isi_fmt *fmt;
	struct media_pad *source_pad;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = interval->index,
		.width = interval->width,
		.height = interval->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	fmt = mxc_isi_find_format(&interval->pixel_format, NULL, 0);
	if (!fmt || fmt->fourcc != interval->pixel_format)
		return -EINVAL;
	fie.code = fmt->mbus_code;

	source_pad = mxc_isi_get_remote_source_pad(mxc_isi);
	if (source_pad == NULL) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, No remote pad found!\n", __func__);
		return -EINVAL;
	}

	/* Get remote source pad subdev */
	sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	if (sd == NULL) {
		v4l2_err(&mxc_isi->isi_cap.sd, "Can't find sensor subdev\n");
		return -ENODEV;
	}

	ret = v4l2_subdev_call(sd, pad, enum_frame_interval, NULL, &fie);
	if (ret)
		return ret;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete = fie.interval;

	return 0;
}

static int mxc_isi_cap_enum_input(struct file *file, void *fh, struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	strscpy(input->name, "camera", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int mxc_isi_cap_g_input(struct file *file, void *fh, unsigned int *input)
{
	*input = 0;

	return 0;
}

static int mxc_isi_cap_s_input(struct file *file, void *fh, unsigned int input)
{
	return input == 0 ? 0 : -EINVAL;
}

static long mxc_isi_cap_default_ioctl(struct file *file, void *fh, bool use_prio,
			unsigned int cmd, void *arg)
{
	struct v4l2_fh *vfh = file->private_data;
	struct video_device *vdev = vfh->vdev;

	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct mxc_isi_cap_dev *isi_cap = &mxc_isi->isi_cap;

	struct v4l2_subdev *sd = mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);

	struct vb2_queue *q = &isi_cap->vb2_q;

	switch (cmd) {
    case VIDIOC_MEM_ALLOC: {
        struct v4l2_dma_mem *mem = arg;
        int ret = 0;
        int count = 1;
        int plane_size[1] = { 0 };

        ret = vb2_core_create_bufs(q, mem->memory, &count, 1, plane_size, true, mem->index);

        if (ret < 0)
            return ret;
        return 0;

        break;
    }
    case VIDIOC_MEM_FREE: {
        struct v4l2_dma_mem *mem = arg;
        int ret = 0;
        ret = vb2_buffer_free(q, mem->index);
        if (ret < 0)
            return ret;
        return 0;
        break;
    }
	case VIDIOC_FLUSH_FRAMES: {
		update_flush_state(mxc_isi, FLUSH_IN_PROGRESS);
		vb2_core_queue_release(q);
		update_flush_state(mxc_isi, FLUSH_DONE);
		return 0;
		break;
	}
	case VIDIOC_STREAMSTAT: {
		struct v4l2_stats_t *stream_stats = arg;
		stream_stats->FramesCount = isi_cap->frame_done_count;
		stream_stats->FramesUnderrun = vdev->qbuf_count - vdev->dqbuf_count;
		//TODO: complete stream statistics
		return 0;
		break;
	}
	case VIDIOC_RESET_STREAMSTAT: {
		memset(&vdev->stream_stats, 0x00, sizeof(struct v4l2_stats_t));
		vdev->qbuf_count = 0;
		vdev->dqbuf_count = 0;
		isi_cap->frame_done_count = 0;
		//isi_cap->frame_interval = 0;
		return 0;
		break;
	}
    case VIDIOC_STREAMON_EX: {
        struct v4l2_streamon_ex *streamon = arg;
        int ret = 0;
        int i = 0;
        struct v4l2_pix_format fmt;
        struct v4l2_subdev_format sd_fmt;

        fmt.width = streamon->nIPU_X;
        fmt.height = streamon->nIPU_Y;
        fmt.sizeimage = streamon->nTotalDataSize;

#if 0
	v4l2_fill_mbus_format(&sd_fmt.format, &fmt, MEDIA_BUS_FMT_UYVY8_2X8);
	v4l2_fill_mbus_format(&sd_fmt.format, &fmt, MEDIA_BUS_FMT_YUYV8_2X8);

	v4l2_fill_mbus_format(&sd_fmt.format, &fmt, isi_cap->mbus_code); isi_frame -> isi_fmt -> mbus_code
	    ret = v4l2_subdev_call(sd, pad, set_fmt, NULL, &sd_fmt);
	pr_err(" -> STREAMON; ret: %d, %s, %d", ret, __func__, __LINE__);

	ret = mx6s_vidioc_streamon(file, vfh, streamon->nBufferType);
#endif
	ret = mxc_isi_cap_streamon(file, vfh, streamon->nBufferType);
	pr_err(" -> STREAMON; ret: %d, %s, %d", ret, __func__, __LINE__);
	pr_err(" -> num_buffers: %d", q->num_buffers);

#if 0
	struct media_entity *entity = &vdev->entity;
	struct v4l2_subdev *subdev;
	subdev = media_entity_to_v4l2_subdev(entity);

	pr_err(" -> %s, %d", __func__, __LINE__)
	    pr_err(" -> num buffers: %d", q->num_buffers);

	ret = mxc_isi_config_parm(mxc_isi);
	if (ret < 0)
	    return -EINVAL;
	for (i = 0; i < q->num_buffers; i++) {
	    ret = vb2_core_qbuf(q, i, NULL);
	    if (ret) {
		pr_err(" -> %s, %d", __func__, __LINE__);
		return ret;
	    }
	}
	ret = vb2_core_streamon(q, streamon->nBufferType);
	pr_err(" -> %s, %d", __func__, __LINE__);
	mxc_isi_channel_enable(mxc_isi);
	mxc_isi_pipeline_enable(mxc_isi, 1);

	mxc_isi->is_streaming = 1;
#endif

        return ret;
        break;
    }
    case VIDIOC_STREAMOFF_EX: {
        struct v4l2_streamoff_ex *streamoff = arg;
        int ret = 0;

		ret = mxc_isi_cap_streamoff(file, vfh, streamoff->nBufferType);

		if (ret < 0)
			return ret;

        return 0;
        break;
    }

    case VIDIOC_G_STATISTIC_CAPABILITIES: {
        struct v4l2_statistics_capabilities *statistics_capabilities = arg;
        statistics_capabilities->nStatisticsCapability = V4L2_STATISTICS_CAPABILITY_FrameCount |
				//TODO: complete statistics capability
				#if 0
                V4L2_STATISTICS_CAPABILITY_FramesIncomplete |
                V4L2_STATISTICS_CAPABILITY_PacketCRCError |
                V4L2_STATISTICS_CAPABILITY_CurrentFrameInterval |
                V4L2_STATISTICS_CAPABILITY_CurrentFrameInterval |
				#endif
                V4L2_STATISTICS_CAPABILITY_FramesUnderrun;
        return 0;
        break;
    }

    case VIDIOC_G_MIN_ANNOUNCED_FRAMES: {
        struct v4l2_min_announced_frames *min_announced_frames = arg;
        min_announced_frames->nMinAnnouncedFrames = MIN_ANNOUNCED_FRAMES;
        return 0;
        break;
    }

    case VIDIOC_G_SUPPORTED_LANE_COUNTS: {

        struct v4l2_supported_lane_counts *lane_counts = arg;

        lane_counts->nSupportedLaneCounts =
            V4L2_LANE_COUNT_1_LaneSupport |
            V4L2_LANE_COUNT_2_LaneSupport |
            V4L2_LANE_COUNT_4_LaneSupport;

        return 0;
    }
	case VIDIOC_G_CSI_HOST_CLK_FREQ: {
        struct v4l2_csi_host_clock_freq_ranges *csi_clk_ranges = arg;

        csi_clk_ranges->lane_range_1.nValid =
            csi_clk_ranges->lane_range_2.nValid =
            csi_clk_ranges->lane_range_4.nValid = 1;
        csi_clk_ranges->lane_range_1.nMin =
            csi_clk_ranges->lane_range_2.nMin =
            csi_clk_ranges->lane_range_4.nMin =
                    CSI_HOST_CLK_MIN_FREQ;
        csi_clk_ranges->lane_range_1.nMax =
            csi_clk_ranges->lane_range_2.nMax =
            csi_clk_ranges->lane_range_4.nMax =
                    CSI_HOST_CLK_MAX_FREQ;

        csi_clk_ranges->lane_range_3.nValid = 0;
        return 0;
        break;
    }
    case VIDIOC_G_IPU_RESTRICTIONS: {
        struct v4l2_ipu_restrictions *ipu_restrictions = arg;

        ipu_restrictions->ipu_x.nValid = 1;
        ipu_restrictions->ipu_x.nMin   = FRAMESIZE_MIN_W;
        ipu_restrictions->ipu_x.nMax   = FRAMESIZE_MAX_W;
        ipu_restrictions->ipu_x.nInc   = FRAMESIZE_INC_W;
        ipu_restrictions->ipu_y.nValid = 1;
        ipu_restrictions->ipu_y.nMin   = FRAMESIZE_MIN_H;
        ipu_restrictions->ipu_y.nMax   = FRAMESIZE_MAX_H;
        ipu_restrictions->ipu_y.nInc   = FRAMESIZE_INC_H;
        return 0;
        break;
    }
    case VIDIOC_G_SUPPORTED_DATA_IDENTIFIERS: {
        struct v4l2_csi_data_identifiers_inq *data_ids = arg;

        data_ids->nDataIdentifiersInq1 = DATA_IDENTIFIER_INQ_1;
        data_ids->nDataIdentifiersInq2 = DATA_IDENTIFIER_INQ_2;
        data_ids->nDataIdentifiersInq3 = DATA_IDENTIFIER_INQ_3;
        data_ids->nDataIdentifiersInq4 = DATA_IDENTIFIER_INQ_4;
        return 0;
        break;
    }
	}
    if (!v4l2_subdev_has_op(sd, core, ioctl))
        return -ENOTTY;

    return v4l2_subdev_call(sd, core, ioctl, cmd, arg);
}

int mxc_isi_cap_ioctl_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2_fh *vfh = file->private_data;
	struct video_device *vdev = vfh->vdev;
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	int ret = 0;

	/* Mono8 workaround */
	if(dst_f->fmt->fourcc == V4L2_PIX_FMT_GREY)
	{
		p->length = 2;
	}

	ret = vb2_ioctl_qbuf(file, priv, p);

	/* Mono8 workaround */
	if(dst_f->fmt->fourcc == V4L2_PIX_FMT_GREY)
	{
		p->length = 1;
	} 

	if (ret < 0)
		return ret;

	vdev->qbuf_count++;

	return 0;
}

int mxc_isi_cap_ioctl_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct v4l2_fh *vfh = file->private_data;
	struct video_device *vdev = vfh->vdev;
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	int ret = 0;

	/* Mono8 workaround */
	if(dst_f->fmt->fourcc == V4L2_PIX_FMT_GREY)
	{
		p->length = 2;
	}

	ret = vb2_ioctl_dqbuf(file, priv, p);

	/* Mono8 workaround */
	if(dst_f->fmt->fourcc == V4L2_PIX_FMT_GREY)
	{
		p->length = 1;
	}

	if (ret < 0)
		return ret;

	vdev->dqbuf_count++;

	return 0;
}

static int mxc_isi_cap_ioctl_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct mxc_isi_dev *mxc_isi = video_drvdata(file);
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	int ret = 0;

	/* Mono8 workaround */
	if(dst_f->fmt->fourcc == V4L2_PIX_FMT_GREY)
	{
		p->length = 2;
	}

	ret = vb2_ioctl_querybuf(file, priv, p);

	/* Mono8 workaround */
	if(dst_f->fmt->fourcc == V4L2_PIX_FMT_GREY)
	{
		p->length = 1;
	}

	return ret;
}

static const struct v4l2_ioctl_ops mxc_isi_capture_ioctl_ops = {
	.vidioc_querycap		= mxc_isi_cap_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= mxc_isi_cap_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= mxc_isi_cap_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= mxc_isi_cap_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= mxc_isi_cap_g_fmt_mplane,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= mxc_isi_cap_ioctl_querybuf,
	.vidioc_qbuf			= mxc_isi_cap_ioctl_qbuf,
	.vidioc_dqbuf			= mxc_isi_cap_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,

	.vidioc_streamon		= mxc_isi_cap_streamon,
	.vidioc_streamoff		= mxc_isi_cap_streamoff,

	.vidioc_g_selection		= mxc_isi_cap_g_selection,
	.vidioc_s_selection		= mxc_isi_cap_s_selection,
	.vidioc_g_chip_ident	= mxc_isi_cap_g_chip_ident,

	.vidioc_g_parm			= mxc_isi_cap_g_parm,
	.vidioc_s_parm			= mxc_isi_cap_s_parm,

	.vidioc_enum_framesizes = mxc_isi_cap_enum_framesizes,
	.vidioc_enum_frameintervals = mxc_isi_cap_enum_frameintervals,

	.vidioc_enum_input		= mxc_isi_cap_enum_input,
	.vidioc_g_input			= mxc_isi_cap_g_input,
	.vidioc_s_input			= mxc_isi_cap_s_input,

	.vidioc_default			= mxc_isi_cap_default_ioctl,
};

int mxc_isi_add_subdev_ctrls(struct mxc_isi_dev *mxc_isi)
{
	struct v4l2_subdev *sensor_sd =
		mxc_isi_get_sensor_subdev(&mxc_isi->isi_cap.sd);
	struct mxc_isi_ctrls *ctrls = &mxc_isi->ctrls;
	struct v4l2_ctrl_handler *handler = &ctrls->handler;
	int ret = 0;

	if (sensor_sd) {
		ret = v4l2_ctrl_add_handler(handler, sensor_sd->ctrl_handler, NULL);
		if (ret || handler->error) {
			dev_err(&mxc_isi->pdev->dev,
				"Failed to add sub-device controls\n");
			return ret;
		}
	}
	return 0;
}

/* Capture subdev media entity operations */
static int mxc_isi_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct mxc_isi_dev *mxc_isi = v4l2_get_subdevdata(sd);

	if (WARN_ON(mxc_isi == NULL))
		return 0;

	if (!(flags & MEDIA_LNK_FL_ENABLED)) {
		return 0;
	}
	/* TODO */
	/* Add ISI source and sink pad link configuration */
	if (local->flags & MEDIA_PAD_FL_SOURCE) {
		switch (local->index) {
		case MXC_ISI_SD_PAD_SOURCE_DC0:
		case MXC_ISI_SD_PAD_SOURCE_DC1:
			break;
		case MXC_ISI_SD_PAD_SOURCE_MEM:
			break;
		default:
			dev_err(&mxc_isi->pdev->dev, "%s invalid source pad\n", __func__);
			return -EINVAL;
		}
	} else if (local->flags & MEDIA_PAD_FL_SINK) {
		switch (local->index) {
		case MXC_ISI_SD_PAD_SINK_MIPI0_VC0:
		case MXC_ISI_SD_PAD_SINK_MIPI0_VC1:
		case MXC_ISI_SD_PAD_SINK_MIPI0_VC2:
		case MXC_ISI_SD_PAD_SINK_MIPI0_VC3:
		case MXC_ISI_SD_PAD_SINK_MIPI1_VC0:
		case MXC_ISI_SD_PAD_SINK_MIPI1_VC1:
		case MXC_ISI_SD_PAD_SINK_MIPI1_VC2:
		case MXC_ISI_SD_PAD_SINK_MIPI1_VC3:
		case MXC_ISI_SD_PAD_SINK_HDMI:
		case MXC_ISI_SD_PAD_SINK_DC0:
		case MXC_ISI_SD_PAD_SINK_DC1:
		case MXC_ISI_SD_PAD_SINK_MEM:
		case MXC_ISI_SD_PAD_SINK_PARALLEL_CSI:
			break;
		default:
			dev_err(&mxc_isi->pdev->dev, "%s invalid sink pad\n", __func__);
			return -EINVAL;
		}
	}

	return 0;
}

static const struct media_entity_operations mxc_isi_sd_media_ops = {
	.link_setup = mxc_isi_link_setup,
};

static int mxc_isi_subdev_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_mbus_code_enum *code)
{

	return 0;
}

static int mxc_isi_subdev_get_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *fmt)
{
	struct mxc_isi_dev *mxc_isi = v4l2_get_subdevdata(sd);
	struct mxc_isi_frame *f;
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	mutex_lock(&mxc_isi->lock);

	switch (fmt->pad) {
	case MXC_ISI_SD_PAD_SOURCE_MEM:
	case MXC_ISI_SD_PAD_SOURCE_DC0:
	case MXC_ISI_SD_PAD_SOURCE_DC1:
		f = &mxc_isi->isi_cap.dst_f;
		break;
	case MXC_ISI_SD_PAD_SINK_MIPI0_VC0:
	case MXC_ISI_SD_PAD_SINK_MIPI0_VC1:
	case MXC_ISI_SD_PAD_SINK_MIPI0_VC2:
	case MXC_ISI_SD_PAD_SINK_MIPI0_VC3:
	case MXC_ISI_SD_PAD_SINK_MIPI1_VC0:
	case MXC_ISI_SD_PAD_SINK_MIPI1_VC1:
	case MXC_ISI_SD_PAD_SINK_MIPI1_VC2:
	case MXC_ISI_SD_PAD_SINK_MIPI1_VC3:
	case MXC_ISI_SD_PAD_SINK_HDMI:
	case MXC_ISI_SD_PAD_SINK_DC0:
	case MXC_ISI_SD_PAD_SINK_DC1:
	case MXC_ISI_SD_PAD_SINK_MEM:
		f = &mxc_isi->isi_cap.src_f;
		break;
	default:
		mutex_unlock(&mxc_isi->lock);
		v4l2_err(mxc_isi->v4l2_dev, "%s, Pad is not support now!\n", __func__);
		return -1;
	}

	if (!WARN_ON(f->fmt == NULL))
		mf->code = f->fmt->mbus_code;

	/* Source/Sink pads crop rectangle size */
	mf->width = f->width;
	mf->height = f->height;

	mutex_unlock(&mxc_isi->lock);
	mf->colorspace = V4L2_COLORSPACE_JPEG;

	return 0;
}

static int mxc_isi_subdev_set_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_format *fmt)
{
	struct mxc_isi_dev *mxc_isi = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct mxc_isi_frame *dst_f = &mxc_isi->isi_cap.dst_f;
	struct mxc_isi_fmt *out_fmt;
	int i;

	if (fmt->pad < MXC_ISI_SD_PAD_SOURCE_MEM &&
					vb2_is_busy(&mxc_isi->isi_cap.vb2_q))
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_out_formats); i++) {
		out_fmt = &mxc_isi_out_formats[i];
		if (mf->code == out_fmt->mbus_code)
			break;
	}
	if (i >= ARRAY_SIZE(mxc_isi_out_formats)) {
		v4l2_err(mxc_isi->v4l2_dev, "%s, format is not support!\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&mxc_isi->lock);
	/* update out put frame size and formate */
	dst_f->fmt = &mxc_isi_out_formats[i];
	set_frame_bounds(dst_f, mf->width, mf->height);
	mutex_unlock(&mxc_isi->lock);

	dev_dbg(&mxc_isi->pdev->dev, "pad%d: code: 0x%x, %dx%d",
	    fmt->pad, mf->code, mf->width, mf->height);

	return 0;
}

static int mxc_isi_subdev_get_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_subdev_selection *sel)
{
	struct mxc_isi_dev *mxc_isi = v4l2_get_subdevdata(sd);
	struct mxc_isi_frame *f = &mxc_isi->isi_cap.src_f;
	struct v4l2_rect *r = &sel->r;
	struct v4l2_rect *try_sel;

	mutex_lock(&mxc_isi->lock);

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		f = &mxc_isi->isi_cap.dst_f;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		r->width = f->o_width;
		r->height = f->o_height;
		r->left = 0;
		r->top = 0;
		mutex_unlock(&mxc_isi->lock);
		return 0;

	case V4L2_SEL_TGT_CROP:
		try_sel = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
		break;
	case V4L2_SEL_TGT_COMPOSE:
		try_sel = v4l2_subdev_get_try_compose(sd, cfg, sel->pad);
		f = &mxc_isi->isi_cap.dst_f;
		break;
	default:
		mutex_unlock(&mxc_isi->lock);
		return -EINVAL;
	}

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		sel->r = *try_sel;
	} else {
		r->left = f->h_off;
		r->top = f->v_off;
		r->width = f->width;
		r->height = f->height;
	}

	dev_dbg(&mxc_isi->pdev->dev, "%s, target %#x: l:%d, t:%d, %dx%d, f_w: %d, f_h: %d",
			__func__, sel->pad, r->left, r->top, r->width, r->height,
			f->c_width, f->c_height);

	mutex_unlock(&mxc_isi->lock);
	return 0;
}

static int mxc_isi_subdev_set_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_subdev_selection *sel)
{
	struct mxc_isi_dev *mxc_isi = v4l2_get_subdevdata(sd);
	struct mxc_isi_frame *f = &mxc_isi->isi_cap.src_f;
	struct v4l2_rect *r = &sel->r;
	struct v4l2_rect *try_sel;
	unsigned long flags;

	mutex_lock(&mxc_isi->lock);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		try_sel = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
		break;
	case V4L2_SEL_TGT_COMPOSE:
		try_sel = v4l2_subdev_get_try_compose(sd, cfg, sel->pad);
		f = &mxc_isi->isi_cap.dst_f;
		break;
	default:
		mutex_unlock(&mxc_isi->lock);
		return -EINVAL;
	}

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		*try_sel = sel->r;
	} else {
		spin_lock_irqsave(&mxc_isi->slock, flags);
		set_frame_crop(f, r->left, r->top, r->width, r->height);
		spin_unlock_irqrestore(&mxc_isi->slock, flags);
	}

	dev_dbg(&mxc_isi->pdev->dev, "%s, target %#x: (%d,%d)/%dx%d", __func__,
			sel->target, r->left, r->top, r->width, r->height);

	mutex_unlock(&mxc_isi->lock);

	return 0;
}

static struct v4l2_subdev_pad_ops mxc_isi_subdev_pad_ops = {
	.enum_mbus_code = mxc_isi_subdev_enum_mbus_code,
	.get_selection = mxc_isi_subdev_get_selection,
	.set_selection = mxc_isi_subdev_set_selection,
	.get_fmt = mxc_isi_subdev_get_fmt,
	.set_fmt = mxc_isi_subdev_set_fmt,
};

static struct v4l2_subdev_ops mxc_isi_subdev_ops = {
	.pad = &mxc_isi_subdev_pad_ops,
};

static int mxc_isi_register_cap_device(struct mxc_isi_dev *mxc_isi,
				 struct v4l2_device *v4l2_dev)
{
	struct video_device *vdev = &mxc_isi->isi_cap.vdev;
	struct vb2_queue *q = &mxc_isi->isi_cap.vb2_q;
	struct mxc_isi_cap_dev *isi_cap = &mxc_isi->isi_cap;
	int ret = -ENOMEM;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);
	memset(vdev, 0, sizeof(*vdev));
	snprintf(vdev->name, sizeof(vdev->name), "mxc_isi.%d.capture", mxc_isi->id);

	/* Reset flush state, because new buffers are enqueued */
	update_flush_state(mxc_isi, FLUSH_NOT_INITIATED);

	vdev->fops	= &mxc_isi_capture_fops;
	vdev->ioctl_ops	= &mxc_isi_capture_ioctl_ops;
	vdev->v4l2_dev	= v4l2_dev;
	vdev->minor	= -1;
	vdev->release	= video_device_release_empty;
	vdev->queue	= q;
	vdev->lock	= &mxc_isi->lock;
	mxc_isi->v4l2_dev = v4l2_dev;

	video_set_drvdata(vdev, mxc_isi);

	INIT_LIST_HEAD(&mxc_isi->isi_cap.out_pending);
	INIT_LIST_HEAD(&mxc_isi->isi_cap.out_active);
	INIT_LIST_HEAD(&mxc_isi->isi_cap.out_discard);

	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = mxc_isi;
	q->ops = &mxc_cap_vb2_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct mxc_isi_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &mxc_isi->lock;

	ret = vb2_queue_init(q);
	if (ret)
		goto err_free_ctx;

	/* Default configuration  */
	isi_cap->dst_f.width = 808;
	isi_cap->dst_f.height = 608;
	isi_cap->dst_f.fmt = isi_cap->src_f.fmt = NULL;

	isi_cap->cap_pad.flags = MEDIA_PAD_FL_SINK;
	vdev->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	ret = media_entity_pads_init(&vdev->entity, 1, &isi_cap->cap_pad);
	if (ret)
		goto err_free_ctx;

	ret = mxc_isi_ctrls_create(mxc_isi);
	if (ret)
		goto err_me_cleanup;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret)
		goto err_ctrl_free;

	vdev->ctrl_handler = &mxc_isi->ctrls.handler;
	v4l2_info(v4l2_dev, "Registered %s as /dev/%s\n",
		  vdev->name, video_device_node_name(vdev));

	return 0;

err_ctrl_free:
	mxc_isi_ctrls_delete(mxc_isi);
err_me_cleanup:
	media_entity_cleanup(&vdev->entity);
err_free_ctx:
	return ret;
}

static int mxc_isi_register_cap_and_m2m_device(struct mxc_isi_dev *mxc_isi,
				 struct v4l2_device *v4l2_dev)
{
	struct mxc_md *mxc_md = container_of(v4l2_dev, struct mxc_md, v4l2_dev);
	int ret;

	ret = mxc_isi_register_cap_device(mxc_isi, v4l2_dev);
	if (ret)
		return ret;

	/* register m2m at last */
	if (!(--mxc_md->nr_isi) && mxc_md->mxc_isi[0]) {
		ret = mxc_isi_register_m2m_device(mxc_md->mxc_isi[0], v4l2_dev);
		if (ret < 0)
			return ret;
		dev_info(&mxc_isi->pdev->dev, "register m2m device success\n");
	}

	return ret;
}

static int mxc_isi_subdev_registered(struct v4l2_subdev *sd)
{
	struct mxc_isi_dev *mxc_isi = v4l2_get_subdevdata(sd);
	int ret;

	if (mxc_isi == NULL)
		return -ENXIO;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);

	ret = mxc_isi_register_cap_and_m2m_device(mxc_isi, sd->v4l2_dev);
	if (ret < 0)
		return ret;

	return 0;
}

static void mxc_isi_subdev_unregistered(struct v4l2_subdev *sd)
{
	struct mxc_isi_dev *mxc_isi = v4l2_get_subdevdata(sd);
	struct video_device *vdev;

	if (mxc_isi == NULL)
		return;

	dev_dbg(&mxc_isi->pdev->dev, "%s\n", __func__);
	mutex_lock(&mxc_isi->lock);

	if (mxc_isi->id == 0 && mxc_isi->skip_m2m == 0)
		mxc_isi_unregister_m2m_device(mxc_isi);

	vdev = &mxc_isi->isi_cap.vdev;
	if (video_is_registered(vdev)) {
		video_unregister_device(vdev);
		mxc_isi_ctrls_delete(mxc_isi);
		media_entity_cleanup(&vdev->entity);
	}

	mutex_unlock(&mxc_isi->lock);
}

static const struct v4l2_subdev_internal_ops mxc_isi_capture_sd_internal_ops = {
	.registered = mxc_isi_subdev_registered,
	.unregistered = mxc_isi_subdev_unregistered,
};

int mxc_isi_initialize_capture_subdev(struct mxc_isi_dev *mxc_isi)
{
	struct v4l2_subdev *sd = &mxc_isi->isi_cap.sd;
	int ret;

	v4l2_subdev_init(sd, &mxc_isi_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "mxc_isi.%d", mxc_isi->id);

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;

	/* ISI Sink pads */
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI0_VC0].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI0_VC1].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI0_VC2].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI0_VC3].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI1_VC0].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI1_VC1].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI1_VC2].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MIPI1_VC3].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_DC0].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_DC1].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_HDMI].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_MEM].flags = MEDIA_PAD_FL_SINK;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SINK_PARALLEL_CSI].flags = MEDIA_PAD_FL_SINK;

	/* ISI source pads */
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SOURCE_MEM].flags = MEDIA_PAD_FL_SOURCE;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SOURCE_DC0].flags = MEDIA_PAD_FL_SOURCE;
	mxc_isi->isi_cap.sd_pads[MXC_ISI_SD_PAD_SOURCE_DC1].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, MXC_ISI_SD_PADS_NUM,
				mxc_isi->isi_cap.sd_pads);
	if (ret)
		return ret;

	sd->entity.ops = &mxc_isi_sd_media_ops;
	sd->internal_ops = &mxc_isi_capture_sd_internal_ops;
	v4l2_set_subdevdata(sd, mxc_isi);

	return 0;
}

void mxc_isi_unregister_capture_subdev(struct mxc_isi_dev *mxc_isi)
{
	struct v4l2_subdev *sd = &mxc_isi->isi_cap.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_set_subdevdata(sd, NULL);
}
