/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2016 Intel Corporation.
 *   All rights reserved.
 */

#include "bdev_xfbd.h"
#include "spdk/util.h"
#include "spdk/uuid.h"
#include "spdk/string.h"
#include "spdk/log.h"

struct rpc_create_xfbd {
	char *bd_name;
	uint32_t block_size;
	char *config_file;
	struct spdk_uuid uuid;
};

static void free_rpc_create_xfbd(struct rpc_create_xfbd *req)
{
	free(req->bd_name);
	free(req->config_file);
}

static const struct spdk_json_object_decoder rpc_create_xfbd_decoders[] = {
	{"bd_name", offsetof(struct rpc_create_xfbd, bd_name), spdk_json_decode_string},
	{"block_size", offsetof(struct rpc_create_xfbd, block_size), spdk_json_decode_uint32},
	{"config_file", offsetof(struct rpc_create_xfbd, config_file), spdk_json_decode_string},
	{"uuid", offsetof(struct rpc_create_xfbd, uuid), spdk_json_decode_uuid, true}
};

static void rpc_bdev_xfbd_create(struct spdk_jsonrpc_request *request,
		    const struct spdk_json_val *params)
{
	struct rpc_create_xfbd req = {};
	struct spdk_json_write_ctx *w;
	struct spdk_bdev *bdev;
	int rc = 0;

	if (spdk_json_decode_object(params, rpc_create_xfbd_decoders,
				    SPDK_COUNTOF(rpc_create_xfbd_decoders),
				    &req)) {
		SPDK_DEBUGLOG(bdev_xfbd, "spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto cleanup;
	}

	rc = bdev_xfbd_create(&bdev, req.config_file, req.bd_name, req.block_size, &req.uuid);
	if (rc) {
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto cleanup;
	}

	w = spdk_jsonrpc_begin_result(request);
	spdk_json_write_string(w, spdk_bdev_get_name(bdev));
	spdk_jsonrpc_end_result(request, w);

cleanup:
	free_rpc_create_xfbd(&req);
}
SPDK_RPC_REGISTER("bdev_xfbd_create", rpc_bdev_xfbd_create, SPDK_RPC_RUNTIME)

struct rpc_bdev_xfbd_delete {
	char *name;
};

static void free_rpc_bdev_xfbd_delete(struct rpc_bdev_xfbd_delete *req)
{
	free(req->name);
}

static const struct spdk_json_object_decoder rpc_bdev_xfbd_delete_decoders[] = {
	{"name", offsetof(struct rpc_bdev_xfbd_delete, name), spdk_json_decode_string}
};

static void _rpc_bdev_xfbd_delete_cb(void *cb_arg, int bdeverrno)
{
	struct spdk_jsonrpc_request *request = cb_arg;

	if (bdeverrno == 0) {
		spdk_jsonrpc_send_bool_response(request, true);
	} else {
		spdk_jsonrpc_send_error_response(request, bdeverrno, spdk_strerror(-bdeverrno));
	}
}

static void rpc_bdev_xfbd_delete(struct spdk_jsonrpc_request *request,
		    const struct spdk_json_val *params)
{
	struct rpc_bdev_xfbd_delete req = {NULL};

	if (spdk_json_decode_object(params, rpc_bdev_xfbd_delete_decoders,
				    SPDK_COUNTOF(rpc_bdev_xfbd_delete_decoders),
				    &req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto cleanup;
	}

	bdev_xfbd_delete(req.name, _rpc_bdev_xfbd_delete_cb, request);
        spdk_jsonrpc_send_bool_response(request, true);
cleanup:
	free_rpc_bdev_xfbd_delete(&req);
}
SPDK_RPC_REGISTER("bdev_xfbd_delete", rpc_bdev_xfbd_delete, SPDK_RPC_RUNTIME)

struct rpc_bdev_xfbd_resize {
	char *name;
	uint64_t new_size;
};

static const struct spdk_json_object_decoder rpc_bdev_xfbd_resize_decoders[] = {
	{"name", offsetof(struct rpc_bdev_xfbd_resize, name), spdk_json_decode_string},
	{"new_size", offsetof(struct rpc_bdev_xfbd_resize, new_size), spdk_json_decode_uint64}
};

static void free_rpc_bdev_xfbd_resize(struct rpc_bdev_xfbd_resize *req)
{
	free(req->name);
}

static void rpc_bdev_xfbd_resize(struct spdk_jsonrpc_request *request,
		    const struct spdk_json_val *params)
{
	struct rpc_bdev_xfbd_resize req = {};
	int rc;

	if (spdk_json_decode_object(params, rpc_bdev_xfbd_resize_decoders,
				    SPDK_COUNTOF(rpc_bdev_xfbd_resize_decoders),
				    &req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto cleanup;
	}

	rc = bdev_xfbd_resize(req.name, req.new_size);
	if (rc) {
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto cleanup;
	}

	spdk_jsonrpc_send_bool_response(request, true);
cleanup:
	free_rpc_bdev_xfbd_resize(&req);
}
SPDK_RPC_REGISTER("bdev_xfbd_resize", rpc_bdev_xfbd_resize, SPDK_RPC_RUNTIME)
