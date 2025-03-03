#include <iostream>

#include <doca_buf.h>

#include "doca_consumer.hpp"
#include "logger.hpp"

DocaConsumer::DocaConsumer(compress_mode compression_task, bool init) : task_type(compression_task) {

    // DOCA-related init from here on
    switch (task_type) {
        case COMPRESS_MODE_COMPRESS_DEFLATE:
            this->supported_check_func = compress_task_compress_deflate_is_supported;
            this->task_submitter = &DocaConsumer::submitCompressDeflateTask;
            strcpy(this->input_file_path, "/tmp/input-decomp");
            strcpy(this->output_file_path, "/tmp/out-comp");
            break;
        case COMPRESS_MODE_DECOMPRESS_DEFLATE:
            this->supported_check_func = compress_task_decompress_deflate_is_supported;
            this->task_submitter = &DocaConsumer::submitDecompressDeflateTask;
            strcpy(this->input_file_path, "/tmp/out-comp");
            strcpy(this->output_file_path, "/tmp/input-decomp");
            break;
        case COMPRESS_MODE_DECOMPRESS_LZ4_STREAM:
            this->supported_check_func = compress_task_decompress_lz4_stream_is_supported;
            this->task_submitter = &DocaConsumer::submitDecompressLz4StreamTask;
            strcpy(this->input_file_path, "/tmp/out-comp");
            strcpy(this->output_file_path, "/tmp/input-decomp");
            break;
        default:
            std::cout << "Wrong processing mode!" << std::endl;
    }

    this->resources.mode = task_type; // TODO: can remove, redundant?
    if (init) {
        this->initDocaContext();
    }
}

void DocaConsumer::initDocaContext() {
    // 1. init DOCA log
    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &this->sdkLog);
    doca_log_backend_set_sdk_level(this->sdkLog, DOCA_LOG_LEVEL_WARNING);

    // 2. pre-allocate resources and set runtime callbacks
    auto result = this->allocateCompressResources();
    if (result != DOCA_SUCCESS) {
        return;
    }

    // 3. determine and allocate output buffer sizes from runtime
    this->getMaxOutputBufferSize();

    // 4. start ctx
    doca_ctx_start(this->resources.state->ctx);

    // 5. open input file
    this->readFile();

    // 6. allocate buffers and prepare mmaps
    this->prepareBuffersAndMmaps();
}

void DocaConsumer::executeDocaTask() {
    // 7. submit appropriate task
    auto result = this->submitTask();
    if (result != DOCA_SUCCESS) {
        std::cout << "DOCA Task finished with errors" << std::endl;
    } else {
        std::cout << "DOCA Task finished successfully" << std::endl;
    }
}

void DocaConsumer::writeDocaResults() {
    // 8. prepare dest buf for writing
    doca_buf_get_data_len(this->dst_doca_buf, &this->input_file_size);

    // 9. write results to output file
    FILE *out_file = fopen(this->output_file_path, "wr");
    if (out_file == nullptr) {
        LOG_ERROR("Unable to open output file: %s", this->output_file_path);
    }

    size_t written_len = fwrite(this->dst_buffer, sizeof(uint8_t), this->input_file_size, out_file);
    if (written_len != this->input_file_size) {
        LOG_ERROR("Failed to write the DOCA buffer representing destination buffer into a file");
        doca_buf_dec_refcount(this->dst_doca_buf, nullptr);
    }
    fclose(out_file);
}


doca_error_t DocaConsumer::allocateCompressResources() {
    this->resources.state = static_cast<program_core_objects *>(malloc(sizeof(*resources.state)));
    if (this->resources.state == nullptr) {
        LOG_ERROR("Failed to allocate DOCA program core objects: %s", doca_error_get_descr(DOCA_ERROR_NO_MEMORY));
        return DOCA_ERROR_NO_MEMORY;
    }

    this->resources.num_remaining_tasks = 0;

    // logic from open_doca_device_with_capabilities without input
    auto result = this->openDocaDeviceWithCapabilities();
    if (result != DOCA_SUCCESS) {
        return DOCA_ERROR_NOT_CONNECTED;
    }

    doca_compress_create(this->resources.state->dev, &this->resources.compress);
    this->resources.state->ctx = doca_compress_as_ctx(this->resources.compress);

    // logic from create_core_objects without input
    this->createCoreObjects();

    // connect to progress engine
    doca_pe_connect_ctx(this->resources.state->pe, this->resources.state->ctx);

    // logic from doca_ctx_set_state_changed_cb, uses compress_resources
    doca_ctx_set_state_changed_cb(this->resources.state->ctx, compress_state_changed_callback);

    switch (task_type) {
        case COMPRESS_MODE_COMPRESS_DEFLATE:
            doca_compress_task_compress_deflate_set_conf(this->resources.compress,
                                                         compress_completed_callback,
                                                         compress_error_callback,
                                                         this->num_compress_tasks);
            break;
        case COMPRESS_MODE_DECOMPRESS_DEFLATE:
            doca_compress_task_decompress_deflate_set_conf(this->resources.compress,
                                                           decompress_deflate_completed_callback,
                                                           decompress_deflate_error_callback,
                                                           this->num_compress_tasks);
            break;
        case COMPRESS_MODE_DECOMPRESS_LZ4_STREAM:
            doca_compress_task_decompress_lz4_stream_set_conf(this->resources.compress,
                                                              decompress_lz4_stream_completed_callback,
                                                              decompress_lz4_stream_error_callback,
                                                              this->num_compress_tasks);
            break;
        default:
            LOG_ERROR("Unknown compress mode: %d", task_type);
            // TODO: free from allocate_compress_resources
            return DOCA_ERROR_INVALID_VALUE;
    }

    /* Include resources in user data of context to be used in callbacks */
    this->ctx_user_data.ptr = &this->resources;
    return doca_ctx_set_user_data(this->resources.state->ctx, this->ctx_user_data);
}

doca_error_t DocaConsumer::openDocaDeviceWithCapabilities() {
    doca_devinfo **dev_list;
    uint32_t nb_devs;

    /* Set default return value */
    this->resources.state->dev = nullptr;

    doca_devinfo_create_list(&dev_list, &nb_devs);

    /* Search */
    for (size_t i = 0; i < nb_devs; i++) {
        /* If any special capabilities are needed */
        if (this->supported_check_func(dev_list[i]) != DOCA_SUCCESS)
            continue;

        /* If device can be opened */
        if (doca_dev_open(dev_list[i], &this->resources.state->dev) == DOCA_SUCCESS) {
            doca_devinfo_destroy_list(dev_list);
            return DOCA_SUCCESS;
        }
    }
    LOG_INFO("Warn: Matching device not found");
    doca_devinfo_destroy_list(dev_list);
    return DOCA_ERROR_NOT_FOUND;
}

doca_error_t DocaConsumer::createCoreObjects() {
    doca_error_t res = doca_mmap_create(&this->resources.state->src_mmap);
    if (res != DOCA_SUCCESS) {
        LOG_ERROR("Unable to create source mmap: %s", doca_error_get_descr(res));
        return res;
    }
    doca_mmap_add_dev(this->resources.state->src_mmap, this->resources.state->dev);
    doca_mmap_create(&this->resources.state->dst_mmap);
    doca_mmap_add_dev(this->resources.state->dst_mmap, this->resources.state->dev);

    if (this->max_bufs != 0) {
        doca_buf_inventory_create(this->max_bufs, &this->resources.state->buf_inv);
        doca_buf_inventory_start(this->resources.state->buf_inv);
    }

    doca_pe_create(&this->resources.state->pe);

    return DOCA_SUCCESS;
}

doca_error_t DocaConsumer::getMaxOutputBufferSize() {
    doca_error_t res = DOCA_ERROR_INVALID_VALUE;
    switch (this->task_type) {
        case COMPRESS_MODE_COMPRESS_DEFLATE:
            res = doca_compress_cap_task_compress_deflate_get_max_buf_size(
                    doca_dev_as_devinfo(this->resources.state->dev),
                    &max_buf_size);
            break;
        case COMPRESS_MODE_DECOMPRESS_DEFLATE:
            res = doca_compress_cap_task_decompress_deflate_get_max_buf_size(
                    doca_dev_as_devinfo(this->resources.state->dev),
                    &max_buf_size);
            break;
        case COMPRESS_MODE_DECOMPRESS_LZ4_STREAM:
            res = doca_compress_cap_task_decompress_lz4_stream_get_max_buf_size(
                    doca_dev_as_devinfo(this->resources.state->dev),
                    &max_buf_size);
            break;
    }

    if (res == DOCA_SUCCESS) {
        this->max_output_size = this->max_buf_size;
    }

    return res;
}

doca_error_t DocaConsumer::prepareBuffersAndMmaps() {
    this->dst_buffer = static_cast<char *>(calloc(1, this->max_output_size));
    doca_mmap_set_memrange(this->resources.state->dst_mmap, this->dst_buffer, this->max_output_size);
    doca_mmap_start(this->resources.state->dst_mmap);
    doca_mmap_set_memrange(this->resources.state->src_mmap, this->input_file_data, this->input_file_size);
    doca_mmap_start(this->resources.state->src_mmap);
    doca_buf_inventory_buf_get_by_addr(this->resources.state->buf_inv, this->resources.state->src_mmap,
                                       this->input_file_data, this->input_file_size, &this->src_doca_buf);
    doca_buf_inventory_buf_get_by_addr(this->resources.state->buf_inv, this->resources.state->dst_mmap,
                                       this->dst_buffer, this->max_buf_size, &this->dst_doca_buf);
    return doca_buf_set_data(this->src_doca_buf, this->input_file_data, this->input_file_size);
    // if (zlib_compatible) {
    // }
}

doca_error_t DocaConsumer::readFile() {
    FILE *file;

    file = fopen(this->input_file_path, "rb");
    if (file == nullptr)
        return DOCA_ERROR_NOT_FOUND;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return DOCA_ERROR_IO_FAILED;
    }

    long const nb_file_bytes = ftell(file);

    if (nb_file_bytes == -1) {
        fclose(file);
        return DOCA_ERROR_IO_FAILED;
    }

    if (nb_file_bytes == 0) {
        fclose(file);
        return DOCA_ERROR_INVALID_VALUE;
    }

    this->input_file_data = static_cast<char *>(malloc(nb_file_bytes));
    if (this->input_file_data == nullptr) {
        fclose(file);
        return DOCA_ERROR_NO_MEMORY;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        this->input_file_data = nullptr;
        fclose(file);
        return DOCA_ERROR_IO_FAILED;
    }

    this->input_file_size = fread(this->input_file_data, 1, nb_file_bytes, file);

    fclose(file);

    if (this->input_file_size != static_cast<size_t>(nb_file_bytes)) {
        this->input_file_data = nullptr;
        return DOCA_ERROR_IO_FAILED;
    }

    return DOCA_SUCCESS;
}

doca_error_t DocaConsumer::submitCompressDeflateTask() {
    doca_data task_user_data = {nullptr};
    doca_task *task;
    timespec ts = {
        .tv_sec = 0,
        .tv_nsec = SLEEP_IN_NANOS,
    };

    doca_compress_task_compress_deflate *compress_deflate_task;
    compress_deflate_result task_result = {DOCA_SUCCESS};
    task_user_data.ptr = &task_result;
    auto result = doca_compress_task_compress_deflate_alloc_init(this->resources.compress,
                                                   this->src_doca_buf,
                                                   this->dst_doca_buf,
                                                   task_user_data,
                                                   &compress_deflate_task);
    if (result != DOCA_SUCCESS) {
        LOG_ERROR("Failed to allocate compress task: %s", doca_error_get_descr(result));
        return result;
    }

    task = doca_compress_task_compress_deflate_as_task(compress_deflate_task);

    /* Submit task */
    this->resources.num_remaining_tasks++;
    result = doca_task_submit(task);
    if (result != DOCA_SUCCESS) {
        LOG_ERROR("Failed to submit compress task: %s", doca_error_get_descr(result));
        doca_task_free(task);
        return result;
    }

    this->resources.run_pe_progress = true;

    /* Wait for all tasks to be completed */
    while (this->resources.run_pe_progress) {
        if (doca_pe_progress(this->resources.state->pe) == 0) {
            nanosleep(&ts, &ts);
        }
    }

    /* Check result of task according to the result we update in the callbacks */
    if (task_result.status != DOCA_SUCCESS) {
        return task_result.status;
    }

    // TODO: calculate checksum if needed
    // if (output_checksum != nullptr)
    //     *output_checksum = calculate_checksum(task_result.crc_cs, task_result.adler_cs);

    return DOCA_SUCCESS;
}

doca_error_t DocaConsumer::submitDecompressDeflateTask() {
    doca_data task_user_data = {nullptr};
    doca_task *task;
    timespec ts = {
        .tv_sec = 0,
        .tv_nsec = SLEEP_IN_NANOS,
    };

    doca_compress_task_decompress_deflate *decompress_deflate_task;
    compress_deflate_result task_result = {DOCA_SUCCESS};
    task_user_data.ptr = &task_result;
    auto result = doca_compress_task_decompress_deflate_alloc_init(this->resources.compress,
                                                     this->src_doca_buf,
                                                     this->dst_doca_buf,
                                                     task_user_data,
                                                     &decompress_deflate_task);
    if (result != DOCA_SUCCESS) {
        LOG_ERROR("Failed to allocate decompress task: %s", doca_error_get_descr(result));
        return result;
    }

    task = doca_compress_task_decompress_deflate_as_task(decompress_deflate_task);

    /* Submit task */
    this->resources.num_remaining_tasks++;
    result = doca_task_submit(task);
    if (result != DOCA_SUCCESS) {
        LOG_ERROR("Failed to submit decompress task: %s", doca_error_get_descr(result));
        doca_task_free(task);
        return result;
    }

    this->resources.run_pe_progress = true;

    /* Wait for all tasks to be completed */
    while (this->resources.run_pe_progress) {
        if (doca_pe_progress(this->resources.state->pe) == 0)
            nanosleep(&ts, &ts);
    }

    /* Check result of task according to the result we update in the callbacks */
    if (task_result.status != DOCA_SUCCESS) {
        return task_result.status;
    }

    // TODO: calculate checksum if needed
    // if (output_checksum != nullptr)
    //     *output_checksum = calculate_checksum(task_result.crc_cs, task_result.adler_cs);

    return DOCA_SUCCESS;
}

doca_error_t DocaConsumer::submitDecompressLz4StreamTask() {
    doca_data task_user_data = {nullptr};
    doca_task *task;
    timespec ts = {
        .tv_sec = 0,
        .tv_nsec = SLEEP_IN_NANOS,
    };

    uint8_t has_block_checksum = 0;
    uint8_t are_blocks_independent = 0;
    doca_compress_task_decompress_lz4_stream *decompress_lz4_stream_task;
    compress_lz4_result task_result = {DOCA_SUCCESS};
    task_user_data.ptr = &task_result;
    auto result = doca_compress_task_decompress_lz4_stream_alloc_init(this->resources.compress,
                                                        has_block_checksum,
                                                        are_blocks_independent,
                                                        this->src_doca_buf,
                                                        this->dst_doca_buf,
                                                        task_user_data,
                                                        &decompress_lz4_stream_task);
    if (result != DOCA_SUCCESS) {
        LOG_ERROR("Failed to allocate decompress task: %s", doca_error_get_descr(result));
        return result;
    }

    task = doca_compress_task_decompress_lz4_stream_as_task(decompress_lz4_stream_task);

    /* Submit task */
    this->resources.num_remaining_tasks++;
    result = doca_task_submit(task);
    if (result != DOCA_SUCCESS) {
        LOG_ERROR("Failed to submit decompress task: %s", doca_error_get_descr(result));
        doca_task_free(task);
        return result;
    }

    this->resources.run_pe_progress = true;

    /* Wait for all tasks to be completed */
    while (this->resources.run_pe_progress) {
        if (doca_pe_progress(this->resources.state->pe) == 0)
            nanosleep(&ts, &ts);
    }

    /* Check result of task according to the result we update in the callbacks */
    if (task_result.status != DOCA_SUCCESS) {
        return task_result.status;
    }

    // TODO: calculate checksum if needed
    // if (output_crc_checksum != nullptr)
    //     *output_crc_checksum = task_result.crc_cs;
    //
    // if (output_xxh_checksum != nullptr)
    //     *output_xxh_checksum = task_result.xxh_cs;

    return DOCA_SUCCESS;
}

DocaConsumer::~DocaConsumer() = default;

std::string DocaConsumer::getName() {
    return "DOCA Consumer";
}
