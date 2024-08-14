#include "spdk/stdinc.h"

#include "bdev_pfbd.h"
#include "spdk/env.h"
#include "spdk/bdev.h"
#include "spdk/thread.h"
#include "spdk/json.h"
#include "spdk/string.h"
#include "spdk/util.h"
#include "spdk/likely.h"

#include "spdk/bdev_module.h"
#include "spdk/log.h"
#include "pf_client_api.h"

static int bdev_pfbd_count = 0;

struct bdev_pfbd {
    struct spdk_bdev disk;
    char *bd_name;
    char *config_file;
    struct PfClientVolume* vol;
};

struct bdev_pfbd_io_channel {
    struct bdev_pfbd *disk;
    struct spdk_io_channel *group_ch;
};

struct bdev_pfbd_io {
    struct spdk_thread *submit_td;
    struct spdk_bdev_io *bdev_io;
    enum spdk_bdev_io_status status;
};

static int bdev_pfbd_group_create_cb(void *io_device, void *ctx_buf)
{
    return 0;
}

static void bdev_pfbd_group_destroy_cb(void *io_device, void *ctx_buf)
{
    return 0;
}

static int bdev_pfbd_init(void);
static void bdev_pfbd_fini(void);
static int bdev_pfbd_get_ctx_size(void)
{
    return sizeof(struct bdev_pfbd_io);
}

static struct spdk_bdev_module xf_if = {
    .name = "bdev_pfbd",
    .module_init = bdev_pfbd_init,
    .module_fini = bdev_pfbd_fini,
    .get_ctx_size = bdev_pfbd_get_ctx_size,
    .async_fini = false,
};

static int bdev_pfbd_init(void)
{
    spdk_io_device_register(&xf_if, bdev_pfbd_group_create_cb, bdev_pfbd_group_destroy_cb,
				0, "bdev_pfbd_poll_groups");
    return 0;
}

static void bdev_pfbd_fini(void)
{
    spdk_io_device_unregister(&xf_if, NULL);
}

static void _bdev_pfbd_io_complete(void *_rbd_io)
{
    struct bdev_pfbd_io *rbd_io = _rbd_io;    
    spdk_bdev_io_complete(spdk_bdev_io_from_ctx(rbd_io), rbd_io->status);
}

static void bdev_pfbd_io_complete(struct spdk_bdev_io *bdev_io, enum spdk_bdev_io_status status)
{
    struct bdev_pfbd_io *rbd_io = (struct bdev_pfbd_io *)bdev_io->driver_ctx;
    struct spdk_thread *current_thread = spdk_get_thread();    
    rbd_io->status = status;
    assert(rbd_io->submit_td != NULL);
    if (rbd_io->submit_td != current_thread) {
    	spdk_thread_send_msg(rbd_io->submit_td, _bdev_pfbd_io_complete, rbd_io);
    } else {
    	_bdev_pfbd_io_complete(rbd_io);
    }
}

static void bdev_pfbd_finish_aiocb(void* data, int comp_status)
{
    struct spdk_bdev_io *bdev_io;
    struct bdev_pfbd_io *rbd_io = data;
    enum spdk_bdev_io_status bio_status;    
    bdev_io = rbd_io->bdev_io;
    bio_status = SPDK_BDEV_IO_STATUS_SUCCESS;    
    if (bdev_io->type == SPDK_BDEV_IO_TYPE_READ) {
    	if (comp_status != 0) {
    	    bio_status = SPDK_BDEV_IO_STATUS_FAILED;
    	}
    } else if (comp_status != 0) { /* For others, 0 means success */
    	bio_status = SPDK_BDEV_IO_STATUS_FAILED;
    }    
    bdev_pfbd_io_complete(bdev_io, bio_status);
}

static void _bdev_pfbd_start_aio(struct bdev_pfbd *disk, struct spdk_bdev_io *bdev_io,
		    struct iovec *iov, int iovcnt, uint64_t offset, size_t len)
{
    int ret;
    struct bdev_pfbd_io *rbd_io = (struct bdev_pfbd_io *)bdev_io->driver_ctx;
    switch (bdev_io->type) {
    case SPDK_BDEV_IO_TYPE_READ:
        ret = pf_iov_submit(disk->vol, iov, iovcnt, len, offset, bdev_pfbd_finish_aiocb, rbd_io, 0);
        if (ret != 0) {
            SPDK_ERRLOG("Failed to submit read, ret=%d", ret);
        }
    	break;
    case SPDK_BDEV_IO_TYPE_WRITE:
        ret = pf_iov_submit(disk->vol, iov, iovcnt, len, offset, bdev_pfbd_finish_aiocb, rbd_io, 1);
        if (ret != 0) {
            SPDK_ERRLOG("Failed to submit write, ret=%d", ret);
        }
    	break;
    case SPDK_BDEV_IO_TYPE_UNMAP:
    case SPDK_BDEV_IO_TYPE_FLUSH:
    case SPDK_BDEV_IO_TYPE_WRITE_ZEROES:
    default:
    	/* This should not happen.
    	 * Function should only be called with supported io types in bdev_rbd_submit_request
    	 */
    	SPDK_ERRLOG("Unsupported IO type =%d\n", bdev_io->type);
    	ret = -ENOTSUP;
    	break;
    }    
    if (ret < 0) {    
    	goto err;
    }    
    return;

err:
    bdev_pfbd_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
}

static void bdev_pfbd_start_aio(void *ctx)
{
    struct spdk_bdev_io *bdev_io = ctx;
    struct bdev_pfbd *disk = (struct bdev_pfbd *)bdev_io->bdev->ctxt;    
    _bdev_pfbd_start_aio(disk,
    		    bdev_io,
    		    bdev_io->u.bdev.iovs,
    		    bdev_io->u.bdev.iovcnt,
    		    bdev_io->u.bdev.offset_blocks * bdev_io->bdev->blocklen,
    		    bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen);
}

static void bdev_pfbd_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
    struct spdk_thread *submit_td = spdk_io_channel_get_thread(ch);
    struct bdev_pfbd_io *rbd_io = (struct bdev_pfbd_io *)bdev_io->driver_ctx;
    rbd_io->submit_td = submit_td;
    rbd_io->bdev_io = bdev_io;
    switch (bdev_io->type) {
        case SPDK_BDEV_IO_TYPE_READ:
        case SPDK_BDEV_IO_TYPE_WRITE:
            bdev_pfbd_start_aio(bdev_io);
            break;
        case SPDK_BDEV_IO_TYPE_UNMAP:
        case SPDK_BDEV_IO_TYPE_FLUSH:
        case SPDK_BDEV_IO_TYPE_WRITE_ZEROES:
        default:
            spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
            break;
    }
}

static bool bdev_pfbd_io_type_supported(void *ctx, enum spdk_bdev_io_type io_type)
{
    switch (io_type) {
        case SPDK_BDEV_IO_TYPE_READ:
        case SPDK_BDEV_IO_TYPE_WRITE:
            return true;
	case SPDK_BDEV_IO_TYPE_UNMAP:
	case SPDK_BDEV_IO_TYPE_FLUSH:
	case SPDK_BDEV_IO_TYPE_RESET:
	case SPDK_BDEV_IO_TYPE_WRITE_ZEROES:
        default:
            return false;
    }
}

static int bdev_pfbd_create_cb(void *io_device, void *ctx_buf)
{
    struct bdev_pfbd_io_channel *ch = ctx_buf;
    struct bdev_pfbd *disk = io_device;    
    ch->disk = disk;
    ch->group_ch = spdk_get_io_channel(&xf_if);
    assert(ch->group_ch != NULL);    
    return 0;
}

static void bdev_pfbd_destroy_cb(void *io_device, void *ctx_buf)
{
    struct bdev_pfbd_io_channel *ch = ctx_buf;
    spdk_put_io_channel(ch->group_ch);
}

static struct spdk_io_channel *bdev_pfbd_get_io_channel(void *ctx)
{
    struct bdev_pfbd *pfbd_bdev = ctx;    
    return spdk_get_io_channel(pfbd_bdev);
}

static void bdev_pfbd_destruct(void *ctx)
{
    // 资源释放逻辑
}

static void bdev_pfbd_dump_info_json(void *ctx, struct spdk_json_write_ctx *w)
{
    struct bdev_pfbd *rbd = ctx;
    spdk_json_write_name(w, "bdev_pfbd");
    spdk_json_write_object_begin(w);
    spdk_json_write_named_string(w, "bd_name", rbd->bd_name);
    spdk_json_write_named_uint32(w, "block_size", rbd->disk.blocklen);
    spdk_json_write_named_string(w, "config_file", rbd->config_file);
    spdk_json_write_named_uuid(w, "uuid", &rbd->disk.uuid);
    spdk_json_write_object_end(w);
}

static void bdev_pfbd_write_config_json(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w)
{
    struct bdev_pfbd *rbd = bdev->ctxt;
    spdk_json_write_object_begin(w);
    spdk_json_write_name(w, "method");
    spdk_json_write_string(w, "bdev_pfbd_create");
    spdk_json_write_name(w, "params");

    spdk_json_write_named_object_begin(w, "params");
    spdk_json_write_named_string(w, "bd_name", rbd->bd_name);
    spdk_json_write_named_uint32(w, "block_size", rbd->disk.blocklen);
    spdk_json_write_named_string(w, "config_file", rbd->config_file);
    spdk_json_write_named_uuid(w, "uuid", &rbd->disk.uuid);
    spdk_json_write_object_end(w);

    spdk_json_write_object_end(w);
}

SPDK_BDEV_MODULE_REGISTER(bdev_pfbd, &xf_if)

static struct spdk_bdev_fn_table bdev_pfbd_fn_table = {
    .destruct = bdev_pfbd_destruct,
    .submit_request = bdev_pfbd_submit_request,
    .io_type_supported = bdev_pfbd_io_type_supported,
    .get_io_channel = bdev_pfbd_get_io_channel,
    .dump_info_json = bdev_pfbd_dump_info_json,
    .write_config_json = bdev_pfbd_write_config_json,
};

static void bdev_pfbd_free(struct bdev_pfbd *rbd)
{
    if (!rbd) {
    	return;
    }
    free(rbd->config_file); 
    free(rbd->bd_name);
    free(rbd);
}

static int bdev_xf_bd_init(struct bdev_pfbd *rbd)
{
    rbd->vol = pf_open_volume(rbd->bd_name, rbd->config_file, NULL, S5_LIB_VER);
    if (rbd->vol == NULL) {
        SPDK_ERRLOG("Failed to open volume:%s!", rbd->bd_name);
        return -1;
    }
    return 0;
}

int bdev_pfbd_create(struct spdk_bdev **bdev, const char *config_file,
                    const char *bd_name,
                    uint32_t block_size,
                    const struct spdk_uuid *uuid)
{
    struct bdev_pfbd *rbd;
    int ret;    
    if ((bd_name == NULL) || (block_size == 0)) {
    	return -EINVAL;
    }    
    rbd = calloc(1, sizeof(struct bdev_pfbd));
    if (rbd == NULL) {
    	SPDK_ERRLOG("Failed to allocate bdev_pfbd struct\n");
    	return -ENOMEM;
    }    
    rbd->config_file = strdup(config_file);
    if (!rbd->config_file) {
    	bdev_pfbd_free(rbd);
    	return -ENOMEM;
    }    
    rbd->bd_name = strdup(bd_name);
    if (!rbd->bd_name) {
    	bdev_pfbd_free(rbd);
    	return -ENOMEM;
    }    
   
    ret = bdev_xf_bd_init(rbd);
    if (ret < 0) {
    	bdev_pfbd_free(rbd);
    	SPDK_ERRLOG("Failed to init pfbd device\n");
    	return ret;
    }    
    rbd->disk.uuid = *uuid;
    if (bd_name) {
    	rbd->disk.name = strdup(bd_name);
    } else {
    	rbd->disk.name = spdk_sprintf_alloc("xfblock%d", bdev_pfbd_count);
    }
    if (!rbd->disk.name) {
    	bdev_pfbd_free(rbd);
    	return -ENOMEM;
    }
    rbd->disk.product_name = "Xflash Rbd Disk";
    bdev_pfbd_count++;    
    rbd->disk.write_cache = 0;
    rbd->disk.blocklen = block_size;
    rbd->disk.blockcnt = pf_get_volume_size(rbd->vol) / rbd->disk.blocklen;
    rbd->disk.ctxt = rbd;
    rbd->disk.fn_table = &bdev_pfbd_fn_table;
    rbd->disk.module = &xf_if;    
    SPDK_NOTICELOG("Add %s pfbd disk to lun\n", rbd->disk.name);    
    spdk_io_device_register(rbd, bdev_pfbd_create_cb,
    			bdev_pfbd_destroy_cb,
    			sizeof(struct bdev_pfbd_io_channel),
    			bd_name);
    ret = spdk_bdev_register(&rbd->disk);
    if (ret) {
    	spdk_io_device_unregister(rbd, NULL);
    	bdev_pfbd_free(rbd);
    	return ret;
    }    
    *bdev = &(rbd->disk);    
    return ret;
}

void bdev_pfbd_delete(const char *bd_name, spdk_delete_pfbd_complete cb_fn,
		     void *cb_arg)
{
    int rc;    
    rc = spdk_bdev_unregister_by_name(bd_name, &xf_if, cb_fn, cb_arg);
    if (rc != 0) {
    	cb_fn(cb_arg, rc);
    }
}

int bdev_pfbd_resize(const char *name, const uint64_t new_size_in_mb)
{
    SPDK_ERRLOG("pfbd nsupport to resize\n");
    return -ENOTSUP;
}

SPDK_LOG_REGISTER_COMPONENT(bdev_pfbd)