#include <errno.h>
#include <limits.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <utils.h>

#include "compress_common.h"
#include "bench_utils.h"

#define BUFFER_SIZE_BF2 134217728 /* Max buffer size in bytes -- BF2 */
#define BUFFER_SIZE_BF3 2097152 /* Max buffer size in bytes -- BF3 */

DOCA_LOG_REGISTER(COMPRESS_DEFLATE_FASTER::MAIN);

struct region {
    uint8_t *base;
    uint32_t size;
};

struct compression_state {
    void *in;
    void *out;
    size_t num_buffers;
    size_t single_buffer_size;
    size_t offloaded;
    size_t completed;

    struct doca_compress *compress;
    struct doca_mmap *mmap_in;
    struct doca_mmap *mmap_out;
    struct doca_buf_inventory *buf_inv;
    struct region *out_regions;

    struct timespec start;
    struct timespec end;
    struct timespec back_to_idle;
};

int offload_next(struct compression_state *state) {
    doca_error_t err;

    size_t num = state->offloaded;
    size_t offset = state->single_buffer_size * num;

    struct doca_buf *buf_in;
    struct doca_buf *buf_out;

    err = doca_buf_inventory_buf_get_by_data(state->buf_inv, state->mmap_in, state->in + offset, state->single_buffer_size, &buf_in);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get inventory input buffer: %s for task: %zu", doca_error_get_descr(err), num);
        goto failure;
    }

    err = doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->mmap_out, state->out + offset, state->single_buffer_size, &buf_out);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get inventory output buffer: %s for task: %zu", doca_error_get_descr(err), num);
        goto failure_buf_in;
    }

    union doca_data task_user_data = { .u64 = num };
    struct doca_compress_task_compress_deflate *compress_task;
    err = doca_compress_task_compress_deflate_alloc_init(state->compress, buf_in, buf_out, task_user_data, &compress_task);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to allocate task %zu: %s", num, doca_error_get_descr(err));
        goto failure_buf_out;
    }

    err = doca_task_submit(doca_compress_task_compress_deflate_as_task(compress_task));
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to submit task %zu: %s", num, doca_error_get_descr(err));
        goto failure_task;
    }

    ++state->offloaded;

    return DOCA_SUCCESS;

failure_task:
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));
failure_buf_out:
    doca_buf_dec_refcount(buf_out, NULL);
failure_buf_in:
    doca_buf_dec_refcount(buf_in, NULL);
failure:
    return err;
}

void compress_state_changed_callback(union doca_data user_data, struct doca_ctx *ctx, 
                                     enum doca_ctx_states prev_state, enum doca_ctx_states next_state) {
    (void) ctx;
    (void) prev_state;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        struct compression_state *state = (struct compression_state *) user_data.ptr;
        clock_gettime(CLOCK_MONOTONIC, &state->start);
        offload_next(state);
    } else if(next_state == DOCA_CTX_STATE_IDLE) {
        struct compression_state *state = (struct compression_state *) user_data.ptr;
        clock_gettime(CLOCK_MONOTONIC, &state->back_to_idle);
    }
}

void chunked_compress_error_callback(struct doca_compress_task_compress_deflate *compress_task,
                                     union doca_data task_user_data, union doca_data ctx_user_data) {
    doca_error_t err = doca_task_get_status(doca_compress_task_compress_deflate_as_task(compress_task));
    size_t num = (size_t) task_user_data.u64;
    DOCA_LOG_ERR("Task %zu failed: %s", num, doca_error_get_descr(err));

    struct doca_buf const *buf_in = doca_compress_task_compress_deflate_get_src(compress_task);
    struct doca_buf *buf_out = doca_compress_task_compress_deflate_get_dst(compress_task);

    doca_buf_dec_refcount((struct doca_buf*) buf_in, NULL);
    doca_buf_dec_refcount(buf_out, NULL);
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));

    struct compression_state *state = (struct compression_state *) ctx_user_data.ptr;
    doca_ctx_stop(doca_compress_as_ctx(state->compress));
}

void chunked_compress_completed_callback(struct doca_compress_task_compress_deflate *compress_task,
                                         union doca_data task_user_data, union doca_data ctx_user_data) {
    size_t num = (size_t) task_user_data.u64;
    struct compression_state *state = (struct compression_state *) ctx_user_data.ptr;

    struct doca_buf const *buf_in = doca_compress_task_compress_deflate_get_src(compress_task);
    struct doca_buf *buf_out = doca_compress_task_compress_deflate_get_dst(compress_task);

    void *out_head;
    size_t out_len;
    doca_buf_get_data(buf_out, &out_head);
    doca_buf_get_data_len(buf_out, &out_len);

    ++state->completed;
    state->out_regions[num].base = out_head;
    state->out_regions[num].size = out_len;

    doca_buf_dec_refcount((struct doca_buf*) buf_in, NULL);
    doca_buf_dec_refcount(buf_out, NULL);
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));

    if(state->offloaded < state->num_buffers) {
        offload_next(state);
    } else if(state->completed == state->num_buffers) {
        clock_gettime(CLOCK_MONOTONIC, &state->end);
        doca_ctx_stop(doca_compress_as_ctx(state->compress));
    }
}

struct doca_compress *open_compress_context(struct doca_dev *dev, struct doca_pe *engine,
                                            struct compression_state *state) {
    struct doca_compress *compress;
    struct doca_ctx *ctx;
    doca_error_t err;

    err = doca_compress_create(dev, &compress);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create context: %s", doca_error_get_descr(err));
        goto failure;
    }

    state->compress = compress;

    ctx = doca_compress_as_ctx(compress);
    err = doca_ctx_set_state_changed_cb(ctx, compress_state_changed_callback);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set state-change callback: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    union doca_data ctx_user_data = { .ptr = state };
    err = doca_ctx_set_user_data(ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set context user data: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_compress_task_compress_deflate_set_conf(compress, chunked_compress_completed_callback, chunked_compress_error_callback, state->num_buffers);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set task callbacks: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_pe_connect_ctx(engine, ctx);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to connect to progress engine: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    err = doca_ctx_start(ctx);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to connect to start context: %s", doca_error_get_descr(err));
        goto failure_cleanup;
    }

    return compress;

failure_cleanup:
    doca_compress_destroy(compress);
failure:
    return NULL;
}

struct doca_mmap *open_memory_map(uint8_t *start, size_t size, struct doca_dev *dev, uint32_t permissions) {
    struct doca_mmap *map;
    doca_error_t err;

    err = doca_mmap_create(&map);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed in creating memory map: %s", doca_error_get_descr(err));
        return NULL;
    }

    err = doca_mmap_set_memrange(map, start, size);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed setting memory range: %s", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_set_permissions(map, permissions);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed setting permissions: %s", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_add_dev(map, dev);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed adding dev: %s", doca_error_get_descr(err));
        goto failure;
    }

    err = doca_mmap_start(map);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed starting mmap: %s", doca_error_get_descr(err));
        goto failure;
    }

    return map;

failure:
    doca_mmap_destroy(map);
    return NULL;
}

struct doca_dev *open_compress_device(void) {
    struct doca_dev *result = NULL;
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;
    doca_error_t err;

    err = doca_devinfo_create_list(&dev_list, &nb_devs);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to get devices: %s", doca_error_get_descr(err));
        return NULL;
    }
    DOCA_LOG_INFO("Number of available devices: %d", nb_devs);

    for(uint32_t i = 0; i < nb_devs; ++i) {
        if(doca_compress_cap_task_compress_deflate_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            err = doca_dev_open(dev_list[i], &result);

            if(err == DOCA_SUCCESS) {
                goto cleanup;
            } else {
                DOCA_LOG_ERR("Failed to open device: %s", doca_error_get_descr(err));
            }
        }
    }
    DOCA_LOG_ERR("No DEFLATE compression device found");

cleanup:
    doca_devinfo_destroy_list(dev_list);
    return result;
}

// EPOLL-based PE. Might be faster with busy-wait (wait on_cond)
struct doca_pe *open_progress_engine(int epoll_fd) {
    struct doca_pe *engine;
    doca_error_t err;

    err = doca_pe_create(&engine);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create progress engine: %s", doca_error_get_descr(err));
        return NULL;
    }

    doca_event_handle_t event_handle;
    err = doca_pe_get_notification_handle(engine, &event_handle);
    if(err != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to obtain notification handle: %s", doca_error_get_descr(err));
        goto failure;
    }

    struct epoll_event events_in = { EPOLLIN, { .fd = event_handle }};
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_handle, &events_in) != 0) {
        DOCA_LOG_ERR("Failed to attach to epoll handle: %s", strerror(errno));
        goto failure;
    }

    return engine;

failure:
    doca_pe_destroy(engine);
    return NULL;
}

struct region *compress_buffers(void *indata, void *outdata, struct region *out_region, 
                                size_t num_buffers, size_t single_buffer_size) {
    struct region *result = NULL;
    struct timespec start_time, end_time_device, end_time_memory, end_time_context, end_time;
    
    clock_gettime(CLOCK_MONOTONIC, &start_time); // start before devices

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if(epoll_fd == -1) {
        DOCA_LOG_ERR("Failed to create epoll file descriptor: %s", strerror(errno));
        goto end;
    }

    struct doca_pe *engine = open_progress_engine(epoll_fd);
    if(!engine) {
        goto cleanup_epoll;
    }

    struct doca_dev *compress_dev = open_compress_device();
    if(!compress_dev) {
        goto cleanup_progress_engine;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time_device); // devices have been dealt with

    struct doca_mmap *mmap_in = open_memory_map(indata, num_buffers * single_buffer_size, compress_dev, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
    if(!mmap_in) {
        goto cleanup_dev;
    }

    struct doca_mmap *mmap_out = open_memory_map(outdata, num_buffers * single_buffer_size, compress_dev, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
    if(!mmap_out) {
        goto cleanup_mmap_in;
    }

    struct doca_buf_inventory *inv;
    doca_error_t doca_errno = doca_buf_inventory_create(num_buffers * 2, &inv);
    if(doca_errno != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed creating buf inventory: %s", doca_error_get_descr(doca_errno));
        goto cleanup_mmap_out;
    }

    doca_errno = doca_buf_inventory_start(inv);
    if(doca_errno != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed starting buf inventory: %s", doca_error_get_descr(doca_errno));
        goto cleanup_inv;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time_memory); // memory has been allocated (mmap and invs)

    struct compression_state state = {
        .in = indata,
        .out = outdata,
        .num_buffers = num_buffers,
        .single_buffer_size = single_buffer_size,
        .offloaded = 0,
        .completed = 0,

        .mmap_in = mmap_in,
        .mmap_out = mmap_out,
        .buf_inv = inv,
        .out_regions = out_region
    };

    struct doca_compress *compress = open_compress_context(compress_dev, engine, &state);
    if(!compress) {
        goto cleanup_inv;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time_context); // time taken to init ctx

    struct epoll_event ep_event = { 0, { 0 } };
    int nfd;

    for(;;) {
        enum doca_ctx_states ctx_state;

        doca_errno = doca_ctx_get_state(doca_compress_as_ctx(compress), &ctx_state);
        if(doca_errno != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to obtain context state: %s", doca_error_get_descr(doca_errno));
            break;
        }

        if(ctx_state == DOCA_CTX_STATE_IDLE) {
            clock_gettime(CLOCK_MONOTONIC, &end_time); // time taken to finalize task end-to-end
            break;  // regular state condition
        }

        doca_pe_request_notification(engine);
        nfd = epoll_wait(epoll_fd, &ep_event, 1, 10);

        if(nfd == -1) {
            DOCA_LOG_ERR("Failed to epoll_wait: %s", doca_error_get_descr(errno));
            goto cleanup_context;
        }

        doca_pe_clear_notification(engine, 0);
        while(doca_pe_progress(engine) > 0) {
            // do nothing; doca_pe_progress calls event handlers
        }
    }

    double total_execution_time = timespec_diff_sec(&end_time, &start_time);
    double task_only_execution_time = timespec_diff_sec(&end_time, &end_time_context);
    double ctx_only_execution_time = timespec_diff_sec(&end_time_context, &end_time_memory);
    double memory_only_execution_time = timespec_diff_sec(&end_time_memory, &end_time_device);
    double device_only_execution_time = timespec_diff_sec(&end_time_device, &start_time);
    DOCA_LOG_INFO("Total time: %.9f seconds", total_execution_time);
    DOCA_LOG_INFO("Task time: %.9f seconds", task_only_execution_time);
    DOCA_LOG_INFO("Ctx time: %.9f seconds", ctx_only_execution_time);
    DOCA_LOG_INFO("Memory time: %.9f seconds", memory_only_execution_time);
    DOCA_LOG_INFO("Device time: %.9f seconds", device_only_execution_time);
    
    double task_exec_time = timespec_diff_sec(&state.end, &state.start);
    if (task_exec_time < 0) {
        DOCA_LOG_WARN("Task time is wrong, end: %ld, start: %ld", state.end.tv_sec, state.start.tv_sec);
    } else {
        double data_rate_from_sec = num_buffers * single_buffer_size / task_exec_time / 1048576.0;
        DOCA_LOG_INFO("Callback task time: %.9f seconds", task_exec_time);
        DOCA_LOG_INFO("Callback task throughput: %.9f mbps", data_rate_from_sec);
        double data_rate_from_task_only = num_buffers * single_buffer_size / task_only_execution_time / 1048576.0;
        DOCA_LOG_INFO("Throughput from caller thread: %.9f mbps", data_rate_from_task_only);
        DOCA_LOG_INFO("Callback task start latency: %.9f seconds", timespec_diff_sec(&end_time_context, &state.start));
        DOCA_LOG_INFO("Callback notification latency: %.9f seconds", timespec_diff_sec(&end_time, &state.back_to_idle));
        DOCA_LOG_INFO("Callback task end latency: %.9f seconds", timespec_diff_sec(&end_time, &state.end));
    }

    result = out_region;

cleanup_context:
    doca_compress_destroy(compress);
cleanup_inv:
    doca_buf_inventory_destroy(inv);
cleanup_mmap_out:
    doca_mmap_destroy(mmap_out);
cleanup_mmap_in:
    doca_mmap_destroy(mmap_in);
cleanup_dev:
    doca_dev_close(compress_dev);
cleanup_progress_engine:
    doca_pe_destroy(engine);
cleanup_epoll:
    close(epoll_fd);
end:

    return result;
}

doca_error_t compress_file(FILE *in, FILE *out, const size_t file_len, size_t single_buffer_size) {
    uint32_t num_buffers;  // Determine number of buffers later

	if (file_len <= single_buffer_size) {
		num_buffers = 1;
		single_buffer_size = file_len;
	} else {
		num_buffers = (uint32_t)(file_len / single_buffer_size);
        if (file_len % single_buffer_size != 0) {
            num_buffers++;  // If there's a remainder, add one extra batch to cover it.
        }
	}

    DOCA_LOG_INFO("Allocated dst buffer number: %d", num_buffers);
    DOCA_LOG_INFO("Allocated dst buffer size: %ld", single_buffer_size);

	// Allocate aligned memory using posix_memalign.
    // Note: The alignment must be a power of two and a multiple of sizeof(void *).
    uint8_t *indata = NULL;
    uint8_t *outdata = NULL;
    int ret = posix_memalign((void **)&indata, 64, num_buffers * single_buffer_size);
    if (ret != 0) {
        DOCA_LOG_ERR("posix_memalign failed for indata: %d", ret);
        return DOCA_ERROR_IO_FAILED;
    }
    ret = posix_memalign((void **)&outdata, 64, num_buffers * single_buffer_size);
    if (ret != 0) {
        DOCA_LOG_ERR("posix_memalign failed for outdata: %d", ret);
        free(indata);
        return DOCA_ERROR_IO_FAILED;
    }

    struct region *region_buffer = calloc(num_buffers, sizeof(struct region));
    if (!region_buffer) {
        DOCA_LOG_ERR("Failed to allocate memory for region buffer");
        goto cleanup;
    }

	// indata and outdata are already aligned on a 64-byte boundary.
	// TODO: fix when num_buffers isn't a power of 2 (or round it)
    size_t read_count = fread(indata, single_buffer_size, num_buffers, in);
    if (read_count != num_buffers) {
        if (num_buffers - read_count == 1) {
            num_buffers = read_count;
        } else {
            DOCA_LOG_ERR("Failed fread on input file; expected %d buffers, got %zu", num_buffers, read_count);
            goto cleanup;
        }
    }

    struct region *out_regions = compress_buffers(indata, outdata, region_buffer, num_buffers, single_buffer_size);
    if(!out_regions) {
        DOCA_LOG_ERR("Failed to compress buffers");
        goto cleanup;
    }

    if(out != NULL) {
        for(uint32_t i = 0; i < num_buffers; ++i) {
            fwrite(&out_regions[i].size, sizeof out_regions[i].size, 1, out);
            fwrite(out_regions[i].base, 1, out_regions[i].size, out);
        }
    }


	DOCA_LOG_INFO("File compressed");
    goto cleanup;

cleanup:
    free(region_buffer);
    free(indata);
    free(outdata);
	return DOCA_SUCCESS;
}

doca_error_t open_input_file(FILE **in, const char *file_path, size_t *file_len) {
    long file_size;
    doca_error_t result = DOCA_SUCCESS;

    /* Open the file for reading */
    *in = fopen(file_path, "r");
    if (*in == NULL) {
        DOCA_LOG_ERR("Failed to open the file %s for reading", file_path);
        return DOCA_ERROR_IO_FAILED;
    }

    /* Calculate the size of the file */
    if (fseek(*in, 0, SEEK_END) != 0) {
        DOCA_LOG_ERR("Failed to calculate file size");
        fclose(*in);
        return DOCA_ERROR_IO_FAILED;
    }

    file_size = ftell(*in);
    if (file_size == -1) {
        DOCA_LOG_ERR("Failed to calculate file size");
        fclose(*in);
        return DOCA_ERROR_IO_FAILED;
    }

    /* Rewind file to the start */
    if (fseek(*in, 0, SEEK_SET) != 0) {
        DOCA_LOG_ERR("Failed to rewind file");
        fclose(*in);
        return DOCA_ERROR_IO_FAILED;
    }

    *file_len = file_size;
    return result;
}

int main(int argc, char **argv) {
	if(argc < 1) {
        fprintf(stderr, "Usage: %s DEVICE [BUFFER_SIZE]\n", argv[0]);
        return EXIT_FAILURE;
    }

	int device = strtol(argv[1], NULL, 10);
	size_t max_buffer_size;
	switch (device) {
		case 2:
			max_buffer_size = BUFFER_SIZE_BF2;
			break;
		case 3:
			max_buffer_size = BUFFER_SIZE_BF3;
			break;
		default:
			fprintf(stderr, "Wrong device id: %d\n", device);
			return EXIT_FAILURE;
	}

	size_t buff_size = max_buffer_size;
	if (argc >= 3) {
		size_t input_buff_size = strtol(argv[2], NULL, 10);
		if (input_buff_size > SIZE_MAX || input_buff_size > max_buffer_size) {
			fprintf(stderr, "BUFFER_SIZE too large, system max: %ld", max_buffer_size);
			return EXIT_FAILURE;
		}
		buff_size = input_buff_size;
	}

	doca_error_t result;
	size_t file_size;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_SUCCESS;
    FILE *ifp = NULL;
    FILE *ofp = NULL;
	char file_path[256] = "/dev/shm/input";
	char output_path[256] = "/dev/shm/input-comp.deflate";

	struct timespec init_start_time, init_end_time;
	clock_gettime(CLOCK_MONOTONIC, &init_start_time);

    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    result = open_input_file(&ifp, file_path, &file_size);
    if (result != DOCA_SUCCESS || ifp == NULL) {
        DOCA_LOG_ERR("failed to open input file");
        fclose(ifp);
        exit_status = result;
    }

    DOCA_LOG_INFO("Starting compression");
	DOCA_LOG_INFO("In compress_file. file size %ld, job type DOCA_COMPRESS_DEFLATE_JOB, compress_method DEFLATE", file_size);

	clock_gettime(CLOCK_MONOTONIC, &init_end_time);
	double init_execution_time = timespec_diff_sec(&init_end_time, &init_start_time);
	DOCA_LOG_INFO("Init time: %.9f seconds. Cleanup time 0 seconds", init_execution_time);

    ofp = fopen(output_path, "wb");
    if(ofp == NULL) {
        DOCA_LOG_WARN("Failed to open %s: %s", file_path, strerror(errno));
    }

	compress_file(ifp, ofp, file_size, buff_size);

    fseek(ofp, 0, SEEK_SET);
    fseek(ofp, 0, SEEK_END);
    size_t compressed_file_size = ftell(ofp);
    DOCA_LOG_INFO("Compressed file size: %ld", compressed_file_size);

    fclose(ifp);
    fclose(ofp);

	return exit_status;
}