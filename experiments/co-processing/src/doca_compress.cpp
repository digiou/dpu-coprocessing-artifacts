#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include "doca_compress.hpp"
#include "logger.hpp"

CompressConsumer::CompressConsumer(DEVICE_TYPE dev_type, uint64_t asked_buffer_size, bool init) {
    strcpy(this->input_file_path, "/dev/shm/input.deflate");
    strcpy(this->output_file_path, "/dev/shm/out-comp.deflate");
    
    switch (dev_type) {
        case DEVICE_TYPE::BF3:
            this->max_buf_size = BUFFER_SIZE_BF3;
            break;
        default:
            this->max_buf_size = BUFFER_SIZE_BF2;
            break;
    }

    this->single_buffer_size = this->max_buf_size;
    if (asked_buffer_size < this->max_buf_size && asked_buffer_size > 0) {
        this->single_buffer_size = asked_buffer_size;
    }

    if (init) {
        this->initDocaContext();
    }
}

void CompressConsumer::initDocaContext() {
    // 1. init DOCA log
    doca_log_backend_create_standard();
    doca_log_backend_create_with_file_sdk(stderr, &this->sdkLog);
    doca_log_backend_set_sdk_level(this->sdkLog, DOCA_LOG_LEVEL_WARNING);
    std::cout << "1. init DOCA log" << std::endl;

    // 2. read file and file size
    auto err = this->readFile();
    if (err != DOCA_SUCCESS) {
        std::cerr << "2. error" << std::endl;
        return;
    }
    // std::cout << "2. read file and file size" << std::endl;

    // 3. determine final buffer size and prepare regions
    err = this->prepareBuffersAndRegions();
    if (err != DOCA_SUCCESS) {
        std::cerr << "3. error" << std::endl;
        return;
    }
    // std::cout << "3. determine final buffer size and prepare regions" << std::endl;

    // 4. prepare progress engine (no epoll)
    err = this->prepareEngine();
    if (err != DOCA_SUCCESS) {
        std::cerr << "4. error" << std::endl;
        return;
    }
    // std::cout << "4. prepare progress engine (no epoll)" << std::endl;

    // 5. open device for compression
    err = this->openDocaDevice();
    if (err != DOCA_SUCCESS) {
        std::cerr << "5. error" << std::endl;
        return;
    }
    // std::cout << "5. open device for compression" << std::endl;

    // 6. prepare mmaps (open memory mmap from C impl)
    err = this->prepareMmaps(DOCA_ACCESS_FLAG_LOCAL_READ_WRITE, DOCA_ACCESS_FLAG_LOCAL_READ_WRITE);
    if (err != DOCA_SUCCESS) {
        std::cerr << "6. error" << std::endl;
        return;
    }
    // std::cout << "6. prepare mmaps (open memory mmap from C impl)" << std::endl;

    // 7. make an inventory
    err = doca_buf_inventory_create(this->num_buffers * 2, &this->inventory);
    if (err != DOCA_SUCCESS) {
        std::cerr << "7.1 error" << std::endl;
        return;
    }
    err = doca_buf_inventory_start(this->inventory);
    if (err != DOCA_SUCCESS) {
        std::cerr << "7.2 error" << std::endl;
        return;
    }
    // std::cout << "7. make an inventory" << std::endl;

    // 8. populate user data object for context
    this->state_obj = {
        .in = this->indata,
        .out = this->outdata,
        .num_buffers = this->num_buffers,
        .single_buffer_size = this->single_buffer_size,
        .offloaded = 0,
        .completed = 0,

        .mmap_in = this->mmap_in,
        .mmap_out = this->mmap_out,
        .buf_inv = this->inventory,
        .out_regions = this->region_buffer
    };
    // std::cout << "8. populate user data object for context" << std::endl;

    // 9. open and start ctx
    err = this->openCompressContext();
    if (err != DOCA_SUCCESS) {
        std::cerr << "9 error" << std::endl;
        return;
    }

    // 10. allocate/prepare tasks from main thread
    this->allocateCompressTasks();
    // std::cout << "10. allocate/prepare tasks from main thread" << std::endl;
}

doca_error_t CompressConsumer::readFile() {
    this->ifp = fopen(this->input_file_path, "r");
    if (this->ifp == nullptr)
        return DOCA_ERROR_NOT_FOUND;

    if (fseek(this->ifp, 0, SEEK_END) != 0) {
        fclose(this->ifp);
        return DOCA_ERROR_IO_FAILED;
    }

    size_t nb_file_bytes = ftell(this->ifp);

    if (nb_file_bytes == -1 || nb_file_bytes == 0) {
        fclose(this->ifp);
        return DOCA_ERROR_IO_FAILED;
    }

    if (fseek(this->ifp, 0, SEEK_SET) != 0) {
        fclose(this->ifp);
        return DOCA_ERROR_IO_FAILED;
    }

    this->input_file_size = nb_file_bytes;
    return DOCA_SUCCESS;
}

doca_error_t CompressConsumer::prepareBuffersAndRegions() {
    if (this->input_file_size <= this->max_buf_size) {
		this->num_buffers = 1;
		this->single_buffer_size = this->input_file_size;
	} else {
		this->num_buffers = static_cast<std::uint32_t>(this->input_file_size / this->single_buffer_size);
        if (this->input_file_size % this->single_buffer_size != 0) {
            this->num_buffers++;  // If there's a remainder, add one extra batch to cover it.
        }
	}
    std::cout << "prepareBuffersAndRegions: " << this->num_buffers << " buffers" << std::endl;

    int ret = posix_memalign((void **)&this->indata, 64, this->num_buffers * this->single_buffer_size);
    if (ret != 0) {
        return DOCA_ERROR_IO_FAILED;
    }
    ret = posix_memalign((void **)&this->outdata, 64, this->num_buffers * this->single_buffer_size);
    if (ret != 0) {
        free(indata);
        return DOCA_ERROR_IO_FAILED;
    }

    this->region_buffer = static_cast<region*>(std::calloc(this->num_buffers, sizeof(struct region)));
    if (!region_buffer) {
        free(region_buffer);
        free(indata);
        free(outdata);
    }

    size_t read_count = fread(indata, single_buffer_size, num_buffers, this->ifp);
    if (read_count != this->num_buffers) {
        if (this->num_buffers - read_count == 1) {
            this->num_buffers = read_count;
        } else {
            free(region_buffer);
            free(indata);
            free(outdata);
        }
    }

    return DOCA_SUCCESS;
}

doca_error_t CompressConsumer::prepareEngine() {
    doca_error_t err;
    err = doca_pe_create(&this->engine);
    if(err != DOCA_SUCCESS) {
        doca_pe_destroy(this->engine);
    }

    return err;
}

doca_error_t CompressConsumer::openDocaDevice() {
    struct doca_devinfo **dev_list;
    uint32_t nb_devs;
    doca_error_t err;

    err = doca_devinfo_create_list(&dev_list, &nb_devs);
    if(err != DOCA_SUCCESS) {
        return err;
    }

    for(uint32_t i = 0; i < nb_devs; ++i) {
        if(doca_compress_cap_task_compress_deflate_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            if(doca_dev_open(dev_list[i], &this->device) == DOCA_SUCCESS) {
                doca_devinfo_destroy_list(dev_list);
                return DOCA_SUCCESS;
            }
        }
    }

    return DOCA_ERROR_NOT_FOUND;
}

doca_error_t CompressConsumer::prepareMmaps(uint32_t in_permissions, uint32_t out_permissions) {
    doca_error_t err;

    // prep input
    err = doca_mmap_create(&this->mmap_in);
    if (err != DOCA_SUCCESS) {
        this->mmap_in = nullptr;
    }

    err = doca_mmap_set_memrange(this->mmap_in, this->indata, this->num_buffers * this->single_buffer_size);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_in);
    }

    err = doca_mmap_set_permissions(this->mmap_in, in_permissions);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_in);
    }

    err = doca_mmap_add_dev(this->mmap_in, this->device);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_in);
    }

    err = doca_mmap_start(this->mmap_in);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_in);
    }

    // prep output
    err = doca_mmap_create(&this->mmap_out);
    if (err != DOCA_SUCCESS) {
        this->mmap_out = nullptr;
    }

    err = doca_mmap_set_memrange(this->mmap_out, this->outdata, this->num_buffers * this->single_buffer_size);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_out);
    }

    err = doca_mmap_set_permissions(this->mmap_out, out_permissions);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_out);
    }

    err = doca_mmap_add_dev(this->mmap_out, this->device);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_out);
    }

    err = doca_mmap_start(this->mmap_out);
    if(err != DOCA_SUCCESS) {
        doca_mmap_destroy(this->mmap_out);
    }

    return err;
}

doca_error_t CompressConsumer::openCompressContext() {
    doca_error_t err;

    err = doca_compress_create(this->device, &this->state_obj.compress);
    if(err != DOCA_SUCCESS) {
        return err;
    }

    ctx = doca_compress_as_ctx(this->state_obj.compress);

    union doca_data ctx_user_data = { .ptr = &this->state_obj };
    err = doca_ctx_set_user_data(this->ctx, ctx_user_data);
    if(err != DOCA_SUCCESS) {
        doca_compress_destroy(this->state_obj.compress);
        return err;
    }

    err = doca_compress_task_compress_deflate_set_conf(this->state_obj.compress, 
                                                       compress_deflate_completed_callback, 
                                                       compress_deflate_error_callback, 
                                                       this->state_obj.num_buffers);
    if(err != DOCA_SUCCESS) {
        doca_compress_destroy(this->state_obj.compress);
        return err;
    }

    err = doca_pe_connect_ctx(this->engine, this->ctx);
    if(err != DOCA_SUCCESS) {
        doca_compress_destroy(this->state_obj.compress);
        return err;
    }

    err = doca_ctx_start(this->ctx);
    if(err != DOCA_SUCCESS) {
        doca_compress_destroy(this->state_obj.compress);
        return err;
    }

    return DOCA_SUCCESS;
}

doca_error_t CompressConsumer::allocateCompressTasks() {
    doca_error_t err;
    // std::cout << "allocateCompressTasks: " << this->state_obj.num_buffers << " buffers" << std::endl;
    // std::cout << "allocateCompressTasks: " << sizeof(*this->state_obj.tasks) << " sizeof(tasks)" << std::endl;
    // std::cout << "allocateCompressTasks: " << this->state_obj.single_buffer_size << " sing buff size" << std::endl;

    uint32_t task_id = 0;
    this->state_obj.tasks = static_cast<doca_compress_task_compress_deflate**>(
        std::calloc(this->state_obj.num_buffers, sizeof(*this->state_obj.tasks))
    );
    // std::cout << "allocateCompressTasks: " << "finished alloc" << std::endl;
    
    for (task_id = 0; task_id < this->state_obj.num_buffers; task_id++) {
        size_t offset = this->state_obj.single_buffer_size * task_id;
        std::cout << "allocateCompressTasks: " << offset << " offset" << std::endl;
        doca_buf *buf_in = nullptr;
        doca_buf *buf_out = nullptr;

        err = doca_buf_inventory_buf_get_by_data(this->state_obj.buf_inv, 
                                                 this->state_obj.mmap_in, 
                                                 static_cast<std::uint8_t*>(this->state_obj.in) + offset, 
                                                 this->state_obj.single_buffer_size, 
                                                 &buf_in);
        if(err != DOCA_SUCCESS) {
            // std::cout << "allocateCompressTasks: " << "failed doca_buf_inventory_buf_get_by_data" << std::endl;
            return err;
        }
        // std::cout << "allocateCompressTasks: " << "finished doca_buf_inventory_buf_get_by_data" << std::endl;

        err = doca_buf_inventory_buf_get_by_addr(this->state_obj.buf_inv, 
                                                 this->state_obj.mmap_out, 
                                                 static_cast<std::uint8_t*>(this->state_obj.out) + offset, 
                                                 this->state_obj.single_buffer_size, 
                                                 &buf_out);
        if(err != DOCA_SUCCESS) {
            doca_buf_dec_refcount(buf_in, nullptr);
            // std::cout << "allocateCompressTasks: " << "failed doca_buf_inventory_buf_get_by_addr" << std::endl;
            return err;
        }
        // std::cout << "allocateCompressTasks: " << "finished doca_buf_inventory_buf_get_by_addr" << std::endl;

        union doca_data task_user_data = { .u64 = task_id };
        err = doca_compress_task_compress_deflate_alloc_init(this->state_obj.compress, 
                                                             buf_in, 
                                                             buf_out, 
                                                             task_user_data, 
                                                             &this->state_obj.tasks[task_id]);
        if(err != DOCA_SUCCESS) {
            doca_buf_dec_refcount(buf_out, nullptr);
            // std::cout << "allocateCompressTasks: " << "failed doca_compress_task_compress_deflate_alloc_init" << std::endl;
            return err;
        }
        // std::cout << "allocateCompressTasks: " << "finished a loop" << std::endl;
    }
    // std::cout << "allocateCompressTasks: " << "finished all loops" << std::endl;

    return err;
}

doca_error_t CompressConsumer::submitCompressTasks() {
	uint32_t task_id = 0;
    doca_error_t err;

	for (task_id = 0; task_id < this->state_obj.num_buffers; task_id++) {
        err = doca_task_submit(doca_compress_task_compress_deflate_as_task(this->state_obj.tasks[task_id]));
        if (err != DOCA_SUCCESS) {
            doca_task_free(doca_compress_task_compress_deflate_as_task(this->state_obj.tasks[task_id]));
            return err;
        }
    }

	return err;
}

doca_error_t CompressConsumer::pollTillCompletion() {
	/* This loop ticks the progress engine */
	while (this->state_obj.completed < this->state_obj.num_buffers) {
		/**
		 * doca_pe_progress shall return 1 if a task was completed and 0 if not. In this case the sample
		 * does not have anything to do with the return value because it is a polling sample.
		 */
		(void)doca_pe_progress(this->engine);
	}

	return DOCA_SUCCESS;
}

void CompressConsumer::executeDocaTask() {
    // 11. submit array of tasks
    this->submit_start = std::chrono::steady_clock::now();

    auto result = this->submitCompressTasks();
    if (result != DOCA_SUCCESS) {
        std::cout << "DOCA Task submission with errors" << std::endl;
    }

    this->submit_end = std::chrono::steady_clock::now();

    // 12. tick and wait for completion
    result = this->pollTillCompletion();
    if (result != DOCA_SUCCESS) {
        std::cout << "DOCA Task polling has errors" << std::endl;
    }

    this->busy_wait_end = std::chrono::steady_clock::now();
}

void CompressConsumer::compress_deflate_completed_callback(struct doca_compress_task_compress_deflate *compress_task,
                                                           union doca_data task_user_data,
                                                           union doca_data ctx_user_data) {
    size_t task_id = (size_t) task_user_data.u64;
    struct compression_state *state = (struct compression_state *) ctx_user_data.ptr;

    struct doca_buf const *buf_in = doca_compress_task_compress_deflate_get_src(compress_task);
    struct doca_buf *buf_out = doca_compress_task_compress_deflate_get_dst(compress_task);

    void *out_head;
    size_t out_len;
    doca_buf_get_data(buf_out, &out_head);
    doca_buf_get_data_len(buf_out, &out_len);

    ++state->completed;
    state->out_regions[task_id].base = static_cast<uint8_t*>(out_head);
    state->out_regions[task_id].size = out_len;

    doca_buf_dec_refcount((struct doca_buf*) buf_in, NULL);
    doca_buf_dec_refcount(buf_out, NULL);
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));

    state->end = std::chrono::steady_clock::now();

    // if(state->offloaded < state->num_buffers) {
    //     offload_next(state);
    // } else if(state->completed == state->num_buffers) {
    //     // clock_gettime(CLOCK_MONOTONIC, &state->end);
    //     doca_ctx_stop(doca_compress_as_ctx(state->compress));
    // }
}

void CompressConsumer::compress_deflate_error_callback(struct doca_compress_task_compress_deflate *compress_task,
                                                       union doca_data task_user_data,
                                                       union doca_data ctx_user_data) {
    (void)task_user_data;

    /* This sample defines that a task is completed even if it is completed with error */
    struct compression_state *state = (struct compression_state *) ctx_user_data.ptr;
    ++state->completed; 

    struct doca_buf const *src = doca_compress_task_compress_deflate_get_src(compress_task);
    struct doca_buf *dst = doca_compress_task_compress_deflate_get_dst(compress_task);

    doca_buf_dec_refcount((struct doca_buf*) src, nullptr);
    doca_buf_dec_refcount(dst, nullptr);
    doca_task_free(doca_compress_task_compress_deflate_as_task(compress_task));

}

void CompressConsumer::compress_deflate_state_changed_callback(union doca_data user_data, struct doca_ctx *ctx, 
                                                doca_ctx_states prev_state, doca_ctx_states next_state) {
    (void) ctx;
    (void) prev_state;

    if(next_state == DOCA_CTX_STATE_RUNNING) {
        struct compression_state *state = (struct compression_state *) user_data.ptr;
        state->start = std::chrono::steady_clock::now();
    }
}

doca_error_t CompressConsumer::cleanup() {

    this->ctx_stop_start = std::chrono::steady_clock::now();
   
    /* A context must be stopped before it is destroyed */
	if (this->ctx != nullptr) {
        (void)doca_ctx_stop(this->ctx);
    }

    /* All contexts must be destroyed before PE is destroyed. Context destroy disconnects it from the PE */
    if (this->state_obj.compress != nullptr) {
        (void) doca_compress_destroy(this->state_obj.compress);
    }

    this->ctx_stop_end = std::chrono::steady_clock::now();

    if (this->engine != nullptr) {
        (void)doca_pe_destroy(this->engine);
    }

	if (this->inventory != nullptr) {
		(void)doca_buf_inventory_stop(this->inventory);
		(void)doca_buf_inventory_destroy(this->inventory);
	}

	if (this->mmap_in != nullptr) {
		(void)doca_mmap_stop(this->mmap_in);
		(void)doca_mmap_destroy(this->mmap_in);
	}

    if (this->mmap_out != nullptr) {
		(void)doca_mmap_stop(this->mmap_out);
		(void)doca_mmap_destroy(this->mmap_out);
	}

	if (this->device != nullptr) {
        (void)doca_dev_close(this->device);
    }

	free(this->region_buffer);
    free(this->indata);
    free(this->outdata);
        
    return DOCA_SUCCESS;
}

std::string CompressConsumer::calculateSeconds(const std::chrono::steady_clock::time_point end,
                                               const std::chrono::steady_clock::time_point start) {
    auto elapsed = end - start;
    auto seconds = std::chrono::duration<double>(elapsed).count();
    // Convert the float to a string with fixed formatting and desired precision
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8) << seconds;
    std::string formattedValue = oss.str();
    return formattedValue;
}

std::vector<std::string> CompressConsumer::getDocaResults() {
    // clean doca structs
    this->cleanup();

    // ctx stop time from cleanup (add to overall later)
    auto ctx_stop_elapsed = this->calculateSeconds(this->ctx_stop_end, this->ctx_stop_start);

    // overall submission time
    auto overall_submission_elapsed = this->calculateSeconds(this->busy_wait_end, this->submit_start);

    // task submission time
    auto task_submission_elapsed = this->calculateSeconds(this->submit_end, this->submit_start);

    // busy-wait time
    auto busy_wait_elapsed = this->calculateSeconds(this->busy_wait_end, this->submit_end);

    // from task submission to last success cb
    auto cb_elapsed = this->calculateSeconds(this->state_obj.end, this->submit_start);

    // from last success cb to busy-wait end
    auto cb_end_elapsed = this->calculateSeconds(this->busy_wait_end, this->state_obj.end);

    // Create vector of results
    return std::vector<std::string>{overall_submission_elapsed, task_submission_elapsed, 
        busy_wait_elapsed, cb_elapsed, cb_end_elapsed, ctx_stop_elapsed};
    
    // 8. prepare dest buf for writing
    // doca_buf_get_data_len(this->dst_doca_buf, &this->input_file_size);

    // 9. write results to output file
    // FILE *out_file = fopen(this->output_file_path, "wr");
    // if (out_file == nullptr) {
    //     LOG_ERROR("Unable to open output file: %s", this->output_file_path);
    // }

    // size_t written_len = fwrite(this->dst_buffer, sizeof(uint8_t), this->input_file_size, out_file);
    // if (written_len != this->input_file_size) {
    //     // LOG_ERROR("Failed to write the DOCA buffer representing destination buffer into a file");
    //     doca_buf_dec_refcount(this->dst_doca_buf, nullptr);
    // }
    // fclose(out_file);
}

CompressConsumer::~CompressConsumer() = default;

std::string CompressConsumer::getName() {
    return "doca-compress";
}