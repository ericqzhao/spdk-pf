/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2016 Intel Corporation.
 *   All rights reserved.
 */

#ifndef SPDK_BDEV_XFBD_H
#define SPDK_BDEV_XFBD_H

#include "spdk/stdinc.h"

#include "spdk/bdev.h"
#include "spdk/rpc.h"

typedef void (*spdk_delete_xfbd_complete)(void *cb_arg, int bdeverrno);

int bdev_xfbd_create(struct spdk_bdev **bdev, const char *config_file,
		    const char *bd_name, uint32_t block_size, const struct spdk_uuid *uuid);
/**
 * Delete xfbd bdev.
 * \param name Bd_name of xfbd bdev.
 * \param cb_fn Function to call after deletion.
 * \param cb_arg Argument to pass to cb_fn.
 */
void bdev_xfbd_delete(const char *bd_name, spdk_delete_xfbd_complete cb_fn,
		     void *cb_arg);

/**
 * Resize xfbd bdev.
 *
 * \param bdev Name of xfbd bdev.
 * \param new_size_in_mb The new size in MiB for this bdev.
 */
int bdev_xfbd_resize(const char *name, const uint64_t new_size_in_mb);
#endif /* SPDK_BDEV_XFBD_H */
