#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#define USER_MAX_FILE_NAME 255                 /* Max file name length */
#define MAX_FILE_NAME (USER_MAX_FILE_NAME + 1) /* Max file name string length */
#define SLEEP_IN_NANOS (10 * 1000)             /* Sample the task every 10 microseconds */

/* Compress modes */
enum compress_mode {
    COMPRESS_MODE_COMPRESS_DEFLATE,      /* Compress mode */
    COMPRESS_MODE_DECOMPRESS_DEFLATE,    /* Decompress mode with deflate algorithm */
    COMPRESS_MODE_DECOMPRESS_LZ4_STREAM, /* Decompress stream mode with lz4 algorithm */
};

/* DOCA core objects */
struct program_core_objects {
    doca_dev *dev;			/* doca device */
    doca_mmap *src_mmap;		/* doca mmap for source buffer */
    doca_mmap *dst_mmap;		/* doca mmap for destination buffer */
    doca_buf_inventory *buf_inv;	/* doca buffer inventory */
    doca_ctx *ctx;			/* doca context */
    doca_pe *pe;			/* doca progress engine */
};

/* DOCA compress resources */
struct compress_resources {
    program_core_objects *state; /* DOCA program core objects */
    doca_compress *compress;     /* DOCA compress context */
    size_t num_remaining_tasks;         /* Number of remaining compress tasks */
    enum compress_mode mode;            /* Compress mode - compress/decompress */
    bool run_pe_progress;               /* Controls whether progress loop should run */
};

/* Describes result of a compress/decompress deflate task from samples/doca_compress/compress_common */
struct compress_deflate_result {
    doca_error_t status; /**< The completion status */
    uint32_t crc_cs;     /**< The CRC checksum */
    uint32_t adler_cs;   /**< The Adler Checksum */
};

/* Describes result of a decompress lz4 tasks from samples/doca_compress/compress_common */
struct compress_lz4_result {
    doca_error_t status; /**< The completion status */
    uint32_t crc_cs;     /**< The CRC checksum */
    uint32_t xxh_cs;     /**< The xxHash Checksum */
};

/* Function to check if a given device is capable of executing some task */
typedef doca_error_t (*tasks_check)(doca_devinfo *);

/* Callbacks that check if a task is supported on a DOCA device */
inline doca_error_t compress_task_compress_deflate_is_supported(doca_devinfo *devinfo) {
    return doca_compress_cap_task_compress_deflate_is_supported(devinfo);
}

inline doca_error_t compress_task_decompress_deflate_is_supported(doca_devinfo *devinfo) {
    return doca_compress_cap_task_decompress_deflate_is_supported(devinfo);
}

inline doca_error_t compress_task_decompress_lz4_stream_is_supported(doca_devinfo *devinfo) {
    return doca_compress_cap_task_decompress_lz4_stream_is_supported(devinfo);
}

// TODO: introduce logging here
/**
 * Callback triggered whenever Compress context state changes.
 * Taken verbatim from compress_common 2.9.0
 *
 * @user_data [in]: User data associated with the Compress context. Will hold struct compress_resources *
 * @ctx [in]: The Compress context that had a state change
 * @prev_state [in]: Previous context state
 * @next_state [in]: Next context state (context is already in this state when the callback is called)
 */
static void compress_state_changed_callback(const doca_data user_data,
                                            doca_ctx *ctx,
                                            doca_ctx_states prev_state,
                                            doca_ctx_states next_state) {
    (void)ctx;
    (void)prev_state;

    compress_resources *resources = static_cast<struct compress_resources *>(user_data.ptr);

    switch (next_state) {
        case DOCA_CTX_STATE_IDLE:
            // LOG_INFO("Compress context has been stopped");
            /* We can stop progressing the PE */
            resources->run_pe_progress = false;
            break;
        case DOCA_CTX_STATE_STARTING:
            /**
             * The context is in starting state, this is unexpected for Compress.
             */
            // LOG_ERROR("Compress context entered into starting state. Unexpected transition");
            break;
        case DOCA_CTX_STATE_RUNNING:
            // LOG_INFO("Compress context is running");
            break;
        case DOCA_CTX_STATE_STOPPING:
            /**
             * doca_ctx_stop() has been called.
             * In this sample, this happens either due to a failure encountered, in which case doca_pe_progress()
             * will cause any inflight task to be flushed, or due to the successful compilation of the sample flow.
             * In both cases, in this sample, doca_pe_progress() will eventually transition the context to idle
             * state.
             */
            // LOG_ERROR("Compress context entered into stopping state. All inflight tasks will be flushed");
            break;
        default:
            break;
    }
}

// TODO: introduce logging here
inline void compress_completed_callback(doca_compress_task_compress_deflate *compress_task,
                                        doca_data task_user_data,
                                        doca_data ctx_user_data) {
    struct compress_resources *resources = static_cast<struct compress_resources *>(ctx_user_data.ptr);
    struct compress_deflate_result *result = static_cast<struct compress_deflate_result *>(task_user_data.ptr);

    // LOG_INFO("Compress task was done successfully");

    /* Prepare task result */
    result->crc_cs = doca_compress_task_compress_deflate_get_crc_cs(compress_task);
    result->adler_cs = doca_compress_task_compress_deflate_get_adler_cs(compress_task);
    result->status = DOCA_SUCCESS;

    /* Free task */
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));
    /* Decrement number of remaining tasks */
    --resources->num_remaining_tasks;

    /* Stop context once all tasks are completed */
    // TODO: Avoid the context stop after a single batch compression
    // the below comment is from hannover chunked compression
    //    if (resources->num_remaining_tasks == 0) {
    //        resources->run_main_loop = false;
    //    }
    // original DOCA code
    if (resources->num_remaining_tasks == 0) {
        (void)doca_ctx_stop(resources->state->ctx);
    }
}

// TODO: introduce logging here
inline void compress_error_callback(doca_compress_task_compress_deflate *compress_task,
                                    doca_data task_user_data,
                                    doca_data ctx_user_data) {
    struct compress_resources *resources = static_cast<struct compress_resources *>(ctx_user_data.ptr);
    struct doca_task *task = doca_compress_task_compress_deflate_as_task(compress_task);
    struct compress_deflate_result *result = static_cast<struct compress_deflate_result *>(task_user_data.ptr);

    /* Get the result of the task */
    result->status = doca_task_get_status(task);
    // LOG_ERROR("Compress task failed: %s", doca_error_get_descr(result->status));
    /* Free task */
    doca_task_free(task);
    /* Decrement number of remaining tasks */
    --resources->num_remaining_tasks;

    /* Stop context once all tasks are completed */
    // TODO: Avoid the context stop after a single batch compression
    // the below comment is from hannover chunked compression
    //    if (resources->num_remaining_tasks == 0) {
    //        resources->run_main_loop = false;
    //    }
    // original DOCA code
    if (resources->num_remaining_tasks == 0) {
        (void)doca_ctx_stop(resources->state->ctx);
    }
}

// TODO: introduce logging here
inline void decompress_deflate_completed_callback(doca_compress_task_decompress_deflate *decompress_task,
                                                  doca_data task_user_data,
                                                  doca_data ctx_user_data) {
    struct compress_resources *resources = static_cast<struct compress_resources *>(ctx_user_data.ptr);
    struct compress_deflate_result *result = static_cast<struct compress_deflate_result *>(task_user_data.ptr);

    // LOG_INFO("Decompress task was done successfully");

    /* Prepare task result */
    result->crc_cs = doca_compress_task_decompress_deflate_get_crc_cs(decompress_task);
    result->adler_cs = doca_compress_task_decompress_deflate_get_adler_cs(decompress_task);
    result->status = DOCA_SUCCESS;

    /* Free task */
    doca_task_free(doca_compress_task_decompress_deflate_as_task(decompress_task));
    /* Decrement number of remaining tasks */
    --resources->num_remaining_tasks;
    /* Stop context once all tasks are completed */
    if (resources->num_remaining_tasks == 0) {
        (void)doca_ctx_stop(resources->state->ctx);
    }
}

// TODO: introduce logging here
inline void decompress_deflate_error_callback(doca_compress_task_decompress_deflate *decompress_task,
                                              doca_data task_user_data,
                                              doca_data ctx_user_data) {
    struct compress_resources *resources = static_cast<struct compress_resources *>(ctx_user_data.ptr);
    struct doca_task *task = doca_compress_task_decompress_deflate_as_task(decompress_task);
    struct compress_deflate_result *result = static_cast<struct compress_deflate_result *>(task_user_data.ptr);

    /* Get the result of the task */
    result->status = doca_task_get_status(task);
    // LOG_ERROR("Decompress task failed: %s", doca_error_get_descr(result->status));
    /* Free task */
    doca_task_free(task);
    /* Decrement number of remaining tasks */
    --resources->num_remaining_tasks;
    /* Stop context once all tasks are completed */
    if (resources->num_remaining_tasks == 0) {
        (void)doca_ctx_stop(resources->state->ctx);
    }
}

// TODO: introduce logging here
inline void decompress_lz4_stream_completed_callback(doca_compress_task_decompress_lz4_stream *decompress_task,
                                                     doca_data task_user_data,
                                                     doca_data ctx_user_data)
{
        compress_resources *resources = static_cast<struct compress_resources *>(ctx_user_data.ptr);
        compress_lz4_result *result = static_cast<struct compress_lz4_result *>(task_user_data.ptr);

        // LOG_INFO("Decompress task was done successfully");

        /* Prepare task result */
        result->crc_cs = doca_compress_task_decompress_lz4_stream_get_crc_cs(decompress_task);
        result->xxh_cs = doca_compress_task_decompress_lz4_stream_get_xxh_cs(decompress_task);
        result->status = DOCA_SUCCESS;

        /* Free task */
        doca_task_free(doca_compress_task_decompress_lz4_stream_as_task(decompress_task));
        /* Decrement number of remaining tasks */
        --resources->num_remaining_tasks;
        /* Stop context once all tasks are completed */
        if (resources->num_remaining_tasks == 0) {
            (void)doca_ctx_stop(resources->state->ctx);
        }
}

// TODO: introduce logging here
inline void decompress_lz4_stream_error_callback(doca_compress_task_decompress_lz4_stream *decompress_task,
                                                 doca_data task_user_data,
                                                 doca_data ctx_user_data)
{
        compress_resources *resources = static_cast<struct compress_resources *>(ctx_user_data.ptr);
        doca_task *task = doca_compress_task_decompress_lz4_stream_as_task(decompress_task);
        compress_lz4_result *result = static_cast<struct compress_lz4_result *>(task_user_data.ptr);

        /* Get the result of the task */
        result->status = doca_task_get_status(task);
        // LOG_ERROR("Decompress task failed: %s", doca_error_get_descr(result->status));
        /* Free task */
        doca_task_free(task);
        /* Decrement number of remaining tasks */
        --resources->num_remaining_tasks;
        /* Stop context once all tasks are completed */
        if (resources->num_remaining_tasks == 0) {
            (void)doca_ctx_stop(resources->state->ctx);
        }
}
