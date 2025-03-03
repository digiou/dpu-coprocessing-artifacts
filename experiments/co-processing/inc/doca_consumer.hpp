#ifndef KAYON_DOCA_CONSUMER_HPP
#define KAYON_DOCA_CONSUMER_HPP

#include <limits>
#include <string>

#include "doca_decls.hpp"

#include <doca_log.h>

// TODO: replace old virtual / next operator methods
class DocaConsumer {
    public:

        explicit DocaConsumer(compress_mode compression, bool init = true);

        ~DocaConsumer();

        std::string getName();

        doca_error_t submitTask() {
            // Call the appropriate setup method and return its result
            return (this->*task_submitter)();
        }

        // 1. init doca logic here (resources, buffers, and context)
        // TODO: copy logic from UT start, compress/decompress_deflate, and allocate_compress_resources (last one first)
        void initDocaContext();

        // 2. single doca task and output here (submit, dest buffer prep, and write)
        void executeDocaTask();

        // 3. write results of task (separate io from processing)
        void writeDocaResults();

        // submit a task for execution
        // TODO: implement as separate methods, std::variant, or polymorph'd classes, anything's better
        doca_error_t (DocaConsumer::*task_submitter)(); // Pointer to the task submit method
        doca_error_t submitCompressDeflateTask();
        doca_error_t submitDecompressDeflateTask();
        doca_error_t submitDecompressLz4StreamTask();

    protected:
        // TODO: generalize to more tasks
        compress_mode task_type;
        tasks_check supported_check_func;
        // TODO: resources might need custom deleter with std::unique_ptr
        // TODO: actually most structs might need custom deleters...
        compress_resources resources = {nullptr};
        int num_compress_tasks = 1;

        // TODO: parameterize or init with defaults, if necessary
        doca_log_backend* sdkLog;
        doca_data ctx_user_data = {nullptr};
        char pci_address[DOCA_DEVINFO_PCI_ADDR_SIZE] = "03:00.0";
        char input_file_path[MAX_FILE_NAME];  /* File to compress/decompress */
        char output_file_path[MAX_FILE_NAME];    /* Output file */
        char *input_file_data = nullptr;
        size_t input_file_size;

        // buffer related limits
        uint32_t max_bufs = 2;
        uint64_t max_buf_size = std::numeric_limits<uint64_t>::min();
        // TODO: add zlib conf for additional mem in max_output_size (zlib_compatible)
        uint64_t max_output_size = std::numeric_limits<uint64_t>::min();

        // memory and doca buffers
        char *dst_buffer;
        doca_buf *src_doca_buf;
        doca_buf *dst_doca_buf;

        // logic from allocate_compress_resources
        doca_error_t allocateCompressResources();

        // logic from open_doca_device_with_capabilities
        doca_error_t openDocaDeviceWithCapabilities();

        // logic from create_core_objects
        // TODO: add buffer free's after checking errors
        doca_error_t createCoreObjects();

        // fetch max allowed buffer size (based on task)
        // TODO: checks for zlib_compatible
        doca_error_t getMaxOutputBufferSize();

        // prepare local and doca buffers (after ctx start)
        // TODO: add doca_buf_dec_refcount for memory areas after checking errors
        // TODO: checks for zlib_compatible
        doca_error_t prepareBuffersAndMmaps();

        // open and read input file
        doca_error_t readFile();
};
#endif //KAYON_DOCA_CONSUMER_HPP
