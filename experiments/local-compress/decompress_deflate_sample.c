#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_ctx.h>
#include <doca_compress.h>
#include <doca_error.h>
#include <doca_log.h>

#include "common.h"
#include "compress_common.h"

static double timespec_diff_sec(const struct timespec *end, const struct timespec *start) {
    double sec  = (double)(end->tv_sec  - start->tv_sec);
    double nsec = (double)(end->tv_nsec - start->tv_nsec);
    return sec + (nsec / 1e9);
}

DOCA_LOG_REGISTER(DECOMPRESS_DEFLATE);

/*
 * Run decompress_deflate sample
 *
 * @cfg [in]: Configuration parameters
 * @file_data [in]: file data for the decompress task
 * @file_size [in]: file size
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t decompress_deflate(struct compress_cfg *cfg, char *file_data, size_t file_size)
{
	struct compress_resources resources = {0};
	struct program_core_objects *state;
	struct doca_buf *src_doca_buf;
	struct doca_buf *dst_doca_buf;
	/* The sample will use 2 doca buffers */
	uint32_t max_bufs = 2;
	uint64_t output_checksum = 0;
	char *dst_buffer;
	size_t data_len, written_len;
	FILE *out_file;
	doca_error_t result, tmp_result;
	uint32_t source_adler_checksum, computed_adler_checksum;
	uint64_t max_buf_size;

	bool zlib_compatible = cfg->is_with_frame;

	DOCA_LOG_INFO("Starting decompression");

	/* In case of compatibility with Zlib - verify the received file is valid */
	if (zlib_compatible) {
		if (file_size < ZLIB_COMPATIBILITY_ADDITIONAL_MEMORY) {
			DOCA_LOG_ERR("Input file length is too short, must be at least %d bytes",
				     ZLIB_COMPATIBILITY_ADDITIONAL_MEMORY);
			return DOCA_ERROR_INVALID_VALUE;
		}

		result = verify_compress_zlib_header((struct compress_zlib_header *)file_data);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to verify Zlib header: %s", doca_error_get_descr(result));
			return result;
		}
	}

	out_file = fopen(cfg->output_path, "wr");
	if (out_file == NULL) {
		DOCA_LOG_ERR("Unable to open output file: %s", cfg->output_path);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Allocate resources */
	resources.mode = COMPRESS_MODE_DECOMPRESS_DEFLATE;
	result = allocate_compress_resources(cfg->pci_address, max_bufs, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate compress resources: %s", doca_error_get_descr(result));
		goto close_file;
	}
	state = resources.state;
	result = doca_compress_cap_task_decompress_deflate_get_max_buf_size(doca_dev_as_devinfo(state->dev),
									    &max_buf_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query decompress max buf size: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}
	if (file_size > max_buf_size) {
		DOCA_LOG_ERR("Invalid file size. Should be smaller then %lu", max_buf_size);
		result = DOCA_ERROR_INVALID_VALUE;
		goto destroy_resources;
	}
	/* Start compress context */
	result = doca_ctx_start(state->ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start context: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	dst_buffer = calloc(1, max_buf_size);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory: %s", doca_error_get_descr(result));
		result = DOCA_ERROR_NO_MEMORY;
		goto destroy_resources;
	}
	DOCA_LOG_INFO("Allocated dst buffer size: %ld", max_buf_size);

	/* start measuring after context is online but before memory alloc */
	struct timespec start_time, end_time_memory, end_time_task, end_time;
	clock_gettime(CLOCK_MONOTONIC, &start_time);

	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, max_buf_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set mmap memory range: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}
	result = doca_mmap_start(state->dst_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start mmap: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}

	result = doca_mmap_set_memrange(state->src_mmap, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set mmap memory range: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}

	result = doca_mmap_start(state->src_mmap);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start mmap: %s", doca_error_get_descr(result));
		goto free_dst_buf;
	}

	/* Construct DOCA buffer for each address range */
	result =
		doca_buf_inventory_buf_get_by_addr(state->buf_inv, state->src_mmap, file_data, file_size, &src_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing source buffer: %s",
			     doca_error_get_descr(result));
		goto free_dst_buf;
	}

	/* Construct DOCA buffer for each address range */
	result = doca_buf_inventory_buf_get_by_addr(state->buf_inv,
						    state->dst_mmap,
						    dst_buffer,
						    max_buf_size,
						    &dst_doca_buf);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to acquire DOCA buffer representing destination buffer: %s",
			     doca_error_get_descr(result));
		goto destroy_src_buf;
	}

	/* Set data length in doca buffer */
	if (zlib_compatible) {
		/* Consider the Zlib header and the added checksum at the end */
		result = doca_buf_set_data(src_doca_buf,
					   (void *)((uint8_t *)file_data + ZLIB_HEADER_SIZE),
					   file_size - ZLIB_COMPATIBILITY_ADDITIONAL_MEMORY);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to get data length in the DOCA buffer representing source buffer: %s",
				     doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	} else {
		result = doca_buf_set_data(src_doca_buf, file_data, file_size);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to get data length in the DOCA buffer representing source buffer: %s",
				     doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	}

	// clock_gettime(CLOCK_MONOTONIC, &end_time_memory);

	/* Submit decompress task with checksum according to user configuration */
	if (cfg->output_checksum || zlib_compatible) {
		result = submit_decompress_deflate_task(&resources, src_doca_buf, dst_doca_buf, &output_checksum, &end_time_memory, &end_time_task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Decompress task failed: %s", doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	} else {
		result = submit_decompress_deflate_task(&resources, src_doca_buf, dst_doca_buf, NULL, &end_time_memory, &end_time_task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Decompress task failed: %s", doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	}

	// clock_gettime(CLOCK_MONOTONIC, &end_time_task);

	/* Verify that the computed Adler checksum matches the given adler checksum */
	if (zlib_compatible) {
		source_adler_checksum = be32toh(*(uint32_t *)((uint8_t *)file_data + file_size - ZLIB_TRAILER_SIZE));
		computed_adler_checksum = (uint32_t)(output_checksum >> ADLER_CHECKSUM_SHIFT);

		if (source_adler_checksum != computed_adler_checksum) {
			DOCA_LOG_ERR(
				"The given Adler checksum=%u, doesn't match the computed Adler checksum=%u. Data may be corrupt",
				source_adler_checksum,
				computed_adler_checksum);
			result = DOCA_ERROR_UNEXPECTED;
			goto destroy_dst_buf;
		}
	}

	/* Write the result to output file */
	result = doca_buf_get_data_len(dst_doca_buf, &data_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to get DOCA buffer data length for destination buffer: %s",
			     doca_error_get_descr(result));
		goto destroy_src_buf;
	}

	clock_gettime(CLOCK_MONOTONIC, &end_time);
	double total_execution_time = timespec_diff_sec(&end_time, &start_time);
	double task_only_execution_time = timespec_diff_sec(&end_time_task, &end_time_memory);
	double memory_only_execution_time = timespec_diff_sec(&end_time, &end_time_task) + timespec_diff_sec(&end_time_memory, &start_time);
	double task_with_memory_init_execution_time = timespec_diff_sec(&end_time_task, &start_time);

	DOCA_LOG_INFO("Decompressed file size: %ld", data_len);
	DOCA_LOG_INFO("Decompression time: %f seconds", total_execution_time);
	DOCA_LOG_INFO("Task time: %f seconds", task_only_execution_time);
	DOCA_LOG_INFO("Memory time: %f seconds", memory_only_execution_time);
	DOCA_LOG_INFO("Task with Memory init time: %f seconds", task_with_memory_init_execution_time);
	DOCA_LOG_INFO("File decompressed");

	written_len = fwrite(dst_buffer, sizeof(uint8_t), data_len, out_file);
	if (written_len != data_len) {
		DOCA_LOG_ERR("Failed to write the DOCA buffer representing destination buffer into a file");
		result = DOCA_ERROR_OPERATING_SYSTEM;
		goto destroy_dst_buf;
	}

	DOCA_LOG_INFO("File was decompressed successfully and saved in: %s", cfg->output_path);
	if (cfg->output_checksum)
		DOCA_LOG_INFO("Checksum is %lu", output_checksum);

destroy_dst_buf:
	tmp_result = doca_buf_dec_refcount(dst_doca_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease DOCA destination buffer reference count: %s",
			     doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
destroy_src_buf:
	tmp_result = doca_buf_dec_refcount(src_doca_buf, NULL);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to decrease DOCA source buffer reference count: %s",
			     doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
free_dst_buf:
	free(dst_buffer);
destroy_resources:
	tmp_result = destroy_compress_resources(&resources);
	if (tmp_result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to destroy compress resources: %s", doca_error_get_descr(tmp_result));
		DOCA_ERROR_PROPAGATE(result, tmp_result);
	}
close_file:
	fclose(out_file);

	return result;
}
