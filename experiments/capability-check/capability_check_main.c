#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>
// #include <doca_regex.h>

#include "common.h"
#include "compress_common.h"

DOCA_LOG_REGISTER(CAPABILITY_CHECK::MAIN);

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv)
{
	struct doca_log_backend *sdk_log;
	// struct compress_resources resources = {0};
	struct doca_devinfo **dev_list;
    uint32_t nb_devs;

	/* Register a logger backend */
	doca_log_backend_create_standard();
	doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);

    DOCA_LOG_INFO("Starting capability check...");

    doca_devinfo_create_list(&dev_list, &nb_devs);

    DOCA_LOG_INFO("Number of available devices: %d", nb_devs);

    for (uint32_t i = 0; i < nb_devs; ++i) {
        DOCA_LOG_INFO("Capability check for device: %d", i);
        if (doca_compress_cap_task_compress_deflate_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            DOCA_LOG_INFO("Device: %d supports COMPRESS_DEFLATE", i);
        }

        if (doca_compress_cap_task_decompress_deflate_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            DOCA_LOG_INFO("Device: %d supports DECOMPRESS_DEFLATE", i);
            uint32_t max_tasks = 1;
            uint32_t max_bufs = 2;
            struct compress_resources resources = {0};
            resources.mode = COMPRESS_MODE_DECOMPRESS_DEFLATE;
            doca_error_t result = allocate_compress_resources(NULL, max_bufs, &resources);
            if (result != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to allocate compress resources: %s", doca_error_get_descr(result));
            }
            doca_compress_cap_get_max_num_tasks(resources.compress, &max_tasks);
            DOCA_LOG_INFO("Device: %d supports %d max (de)compress tasks", i, max_tasks);
        }

        if (doca_compress_cap_task_decompress_lz4_stream_is_supported(dev_list[i]) == DOCA_SUCCESS) {
            DOCA_LOG_INFO("Device: %d supports DECOMPRESS_LZ4_STREAM", i);
        }

        // if (doca_regex_is_supported(dev_list[i]) == DOCA_SUCCESS) {
        //     DOCA_LOG_INFO("Device: %d supports REGEX", i);
        // }

        // if (doca_regex_get_hardware_supported(dev_list[i]) == DOCA_SUCCESS) {
        //     DOCA_LOG_INFO("Device: %d supports hardware REGEX", i);
        // }
    }

    doca_devinfo_destroy_list(dev_list);
	return EXIT_SUCCESS;
}
