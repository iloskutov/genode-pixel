/*
 * \brief  Compile-time adaptation of the vendor trusty-virtio.c (force-included)
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The transport installs private DMA ops on its platform device: the
 * "DMA address" of a TIPC buffer in a virtqueue is the shared-memory id of
 * the buffer, not an address. The shared kernel configuration has no
 * CONFIG_DMA_OPS, so 'set_dma_ops' is an empty inline and the shadow DMA code
 * knows no per-device ops. The ops are recorded in lx_emul.c instead and
 * honoured by the virtio ring (pixel_audio_dma_map_page_attrs).
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__TRUSTY_VIRTIO_SHIM_H_
#define _PIXEL_AUDIO__TRUSTY_VIRTIO_SHIM_H_

#include <linux/dma-map-ops.h>

void pixel_audio_set_dma_ops(struct device *dev,
                             const struct dma_map_ops *ops);

#define set_dma_ops pixel_audio_set_dma_ops

#endif /* _PIXEL_AUDIO__TRUSTY_VIRTIO_SHIM_H_ */
