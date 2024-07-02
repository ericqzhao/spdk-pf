/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2016 Intel Corporation.
 *   All rights reserved.
 */

#ifndef SPDK_BDEV_PFBD_H
#define SPDK_BDEV_PFBD_H

#include "spdk/stdinc.h"

#include "spdk/bdev.h"
#include "spdk/rpc.h"

typedef void (*spdk_delete_pfbd_complete)(void *cb_arg, int bdeverrno);

int bdev_pfbd_create(struct spdk_bdev **bdev, const char *config_file,
		    const char *bd_name, uint32_t block_size, const struct spdk_uuid *uuid);
/**
 * Delete pfbd bdev.
 * \param name Bd_name of pfbd bdev.
 * \param cb_fn Function to call after deletion.
 * \param cb_arg Argument to pass to cb_fn.
 */
void bdev_pfbd_delete(const char *bd_name, spdk_delete_pfbd_complete cb_fn,
		     void *cb_arg);

/**
 * Resize pfbd bdev.
 *
 * \param bdev Name of pfbd bdev.
 * \param new_size_in_mb The new size in MiB for this bdev.
 */
int bdev_pfbd_resize(const char *name, const uint64_t new_size_in_mb);
#endif /* SPDK_BDEV_PFBD_H */
