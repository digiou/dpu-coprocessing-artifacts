#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <doca_argp.h>
#include <doca_compress.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>

#include <utils.h>

#include "compress_common.h"

#include "bench_utils.h"

DOCA_LOG_REGISTER(DECOMPRESS_LZ4_STREAM::MAIN);

/* Sample's Logic */
doca_error_t decompress_lz4_stream(struct compress_cfg *cfg, char *file_data, size_t file_size);

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int main(int argc, char **argv)
{
	doca_error_t result;
	struct compress_cfg compress_cfg;
	char *file_data = NULL;
	size_t file_size;
	struct doca_log_backend *sdk_log;
	int exit_status = EXIT_FAILURE;

	strcpy(compress_cfg.pci_address, "03:00.0");
	strcpy(compress_cfg.file_path, "/dev/shm/input-comp.lz4");
	strcpy(compress_cfg.output_path, "/dev/shm/output-decomp.lz4");
	compress_cfg.is_with_frame = true;
	compress_cfg.has_block_checksum = false;
	compress_cfg.are_blocks_independent = false;
	compress_cfg.output_checksum = false;

	struct timespec init_start_time, init_end_time;
	clock_gettime(CLOCK_MONOTONIC, &init_start_time);

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	/* Register a logger backend for internal SDK errors and warnings */
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto sample_exit;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		goto sample_exit;

	DOCA_LOG_INFO("Starting decompression");

	result = doca_argp_init("doca_decompress_lz4_stream", &compress_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto sample_exit;
	}

	result = register_compress_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register ARGP params: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = register_lz4_stream_params();
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to register ARGP params for lz4 stream tasks: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}

	result = read_file(compress_cfg.file_path, &file_data, &file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to read file: %s", doca_error_get_descr(result));
		goto argp_cleanup;
	}
	DOCA_LOG_INFO("In compress_file. file size %ld, job type DOCA_DECOMPRESS_LZ4_JOB, decompress_method LZ4", file_size);

	clock_gettime(CLOCK_MONOTONIC, &init_end_time);
	double init_execution_time = timespec_diff_sec(&init_end_time, &init_start_time);

	result = decompress_lz4_stream(&compress_cfg, file_data, file_size);
	DOCA_LOG_INFO("Init time: %f seconds. Cleanup time 0 seconds", init_execution_time);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("decompress_lz4_stream() encountered an error: %s", doca_error_get_descr(result));
		goto data_file_cleanup;
	}

	exit_status = EXIT_SUCCESS;

data_file_cleanup:
	if (file_data != NULL)
		free(file_data);
argp_cleanup:
	doca_argp_destroy();
sample_exit:
	if (exit_status == EXIT_SUCCESS)
		DOCA_LOG_INFO("Sample finished successfully");
	else
		DOCA_LOG_INFO("Sample finished with errors");
	return exit_status;
}
