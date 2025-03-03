#ifndef KAYON_DOCA_DECOMPRESS_LZ4_HPP
#define KAYON_DOCA_DECOMPRESS_LZ4_HPP

#include <chrono>
#include <cstdint> // preferred in C++
#include <cstdio>  // if using printf, fopen, etc
#include <limits>
#include <string>
#include <vector>


#include <doca_log.h>
#include <doca_pe.h>

#define USER_MAX_FILE_NAME 255                 /* Max file name length */
#define MAX_FILE_NAME (USER_MAX_FILE_NAME + 1) /* Max file name string length */
#define SLEEP_IN_NANOS (10 * 1000)             /* Sample the task every 10 microseconds */
#define BUFFER_SIZE_BF2 134217728 /* Max buffer size in bytes -- BF2 */
#define BUFFER_SIZE_BF3 2097152 /* Max buffer size in bytes -- BF3 */

struct region {
    uint8_t *base;
    uint32_t size;
};

struct compression_state {
    void *in;
    void *out;
    size_t num_buffers;
    size_t input_buffer_size;
    size_t output_buffer_size;
    size_t offloaded;
    size_t completed;

    struct doca_compress *compress;
    struct doca_mmap *mmap_in;
    struct doca_mmap *mmap_out;
    struct doca_buf_inventory *buf_inv;
    struct region *out_regions;
    struct doca_compress_task_decompress_lz4_block **tasks;

    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    struct timespec back_to_idle;
};

class DecompressLz4Consumer {
    public:
        enum DEVICE_TYPE { BF2, BF3 };

        explicit DecompressLz4Consumer(DEVICE_TYPE dev_type, uint64_t asked_buffer_size, 
            uint64_t asked_num_buffers, size_t original_file_size, bool init = true);

        ~DecompressLz4Consumer();

        std::string getName();

        // 1. init doca logic here (resources, buffers, and context)
        // TODO: copy logic from UT start, compress/decompress_deflate, and allocate_compress_resources (last one first)
        void initDocaContext();

        // 2. single doca task and output here (submit, dest buffer prep, and write)
        void executeDocaTask();

        // 3. write results of task (separate io from processing)
        std::vector<std::string> getDocaResults();

    protected:
        int num_compress_tasks = 1;

        doca_log_backend* sdkLog;
        doca_data ctx_user_data = {nullptr};
        char input_file_path[MAX_FILE_NAME];  /* File to compress/decompress */
        char output_file_path[MAX_FILE_NAME];    /* Output file */
        char *input_file_data = nullptr;
        FILE *ifp = NULL;
        size_t input_file_size, original_file_size;

        // buffer related limits
        uint32_t max_bufs = 2;
        uint32_t num_buffers = 2;

        uint64_t max_buf_size = std::numeric_limits<uint64_t>::min();
        uint64_t single_buffer_size = std::numeric_limits<uint64_t>::min();
        uint64_t output_buffer_size = std::numeric_limits<uint64_t>::min();
        uint64_t input_buff_size = std::numeric_limits<uint64_t>::min();
        // TODO: add zlib conf for additional mem in max_output_size (zlib_compatible)
        uint64_t max_output_size = std::numeric_limits<uint64_t>::min();

        // memory and doca buffers
        char *dst_buffer;
        doca_buf *src_doca_buf;
        doca_buf *dst_doca_buf;
        // compression state obj
        compression_state state_obj;

        // Allocate aligned memory using posix_memalign on indata/outdata.
        uint8_t *indata = nullptr;
        uint8_t *outdata = nullptr;
        // memory areas with input/output raw data and their pointers
        region *region_buffer;
        // doca mmaps
        doca_mmap *mmap_in = nullptr;
        doca_mmap *mmap_out = nullptr;

        // progress engine
        doca_pe *engine = nullptr;

        // device
        doca_dev *device = nullptr;

        // DOCA buffer inventory
        doca_buf_inventory *inventory;

        // DOCA compression context (1 per thread)
        struct doca_ctx *ctx = nullptr;

        // time counters
        std::chrono::steady_clock::time_point submit_start, submit_end, busy_wait_end, ctx_stop_start, ctx_stop_end;

        // logic from open_doca_device_with_capabilities
        doca_error_t openDocaDevice();

        // open and read input file
        doca_error_t readFile();

        // determine buffers and regions to be mmap'd later (after ctx start)
        doca_error_t prepareBuffersAndRegions();

        // prepare engine
        doca_error_t prepareEngine();

        // prepare mmaps
        doca_error_t prepareMmaps(uint32_t in_permissions, uint32_t out_permissions);

        // prepare and init context (with callbacks)
        doca_error_t openCompressContext();

        // prepare compress tasks
        doca_error_t allocateCompressTasks();

        // fire compress tasks
        doca_error_t submitCompressTasks();

        // poll until we drain all tasks
        doca_error_t pollTillCompletion();

        // DOCA task completed callback
        static void decompress_lz4_completed_callback(
            struct doca_compress_task_decompress_lz4_block *compress_task,
            union doca_data task_user_data,
            union doca_data ctx_user_data);

        // DOCA task error callback
        static void decompress_lz4_error_callback(
            struct doca_compress_task_decompress_lz4_block *compress_task,
            union doca_data task_user_data,
            union doca_data ctx_user_data);

        // DOCA task state changed callback
        static void decompress_lz4_state_changed_callback(
            union doca_data user_data, 
            struct doca_ctx *ctx, 
            enum doca_ctx_states prev_state, 
            enum doca_ctx_states next_state);

        // cleanup resources in reverse init order
        doca_error_t cleanup();

        // get diff of two time points
        std::string calculateSeconds(const std::chrono::steady_clock::time_point start,
                                const std::chrono::steady_clock::time_point end);
};
#endif //KAYON_DOCA_DECOMPRESS_LZ4_HPP
