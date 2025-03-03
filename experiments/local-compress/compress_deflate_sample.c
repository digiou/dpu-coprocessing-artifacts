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

#include "bench_utils.h"

DOCA_LOG_REGISTER(COMPRESS_DEFLATE);

/*
 * Run compress_deflate sample
 *
 * @cfg [in]: Configuration parameters
 * @file_data [in]: file data for the compress task
 * @file_size [in]: file size
 * @return: DOCA_SUCCESS on success, DOCA_ERROR otherwise.
 */
doca_error_t compress_deflate(struct compress_cfg *cfg, char *file_data, size_t file_size)
{
	struct compress_resources resources = {0};
	struct program_core_objects *state;
	struct doca_buf *src_doca_buf;
	struct doca_buf *dst_doca_buf;
	/* The sample will use 2 doca buffers */
	uint32_t max_bufs = 2;
	uint64_t output_checksum = 0;
	uint32_t adler_checksum = 0;
	doca_be32_t be_adler_checksum;
	char *dst_buffer;
	void *dst_buf_data, *dst_buf_tail;
	size_t data_len, write_len, written_len;
	FILE *out_file;
	doca_error_t result, tmp_result;
	uint64_t max_buf_size, max_output_size;

	bool zlib_compatible = cfg->is_with_frame;

	DOCA_LOG_INFO("Starting compression");

	out_file = fopen(cfg->output_path, "wr");
	if (out_file == NULL) {
		DOCA_LOG_ERR("Unable to open output file: %s", cfg->output_path);
		return DOCA_ERROR_NO_MEMORY;
	}

	/* Allocate resources */
	resources.mode = COMPRESS_MODE_COMPRESS_DEFLATE;
	result = allocate_compress_resources(cfg->pci_address, max_bufs, &resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to allocate compress resources: %s", doca_error_get_descr(result));
		goto close_file;
	}
	state = resources.state;

	result = doca_compress_cap_task_decompress_deflate_get_max_buf_size(doca_dev_as_devinfo(state->dev),
									    &max_buf_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to query compress max buf size: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}
	if (file_size > max_buf_size) {
		DOCA_LOG_ERR("Invalid file size. Should be smaller than %lu", max_buf_size);
		result = DOCA_ERROR_INVALID_VALUE;
		goto destroy_resources;
	}

	max_output_size = max_buf_size;
	/* Consider the Zlib header and the added checksum at the end */
	if (zlib_compatible) {
		DOCA_LOG_INFO("Program is zlib-compatible.");
		max_output_size += ZLIB_COMPATIBILITY_ADDITIONAL_MEMORY;
	}

	/* Start compress context */
	result = doca_ctx_start(state->ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to start context: %s", doca_error_get_descr(result));
		goto destroy_resources;
	}

	dst_buffer = calloc(1, max_output_size);
	if (dst_buffer == NULL) {
		DOCA_LOG_ERR("Failed to allocate memory: %s", doca_error_get_descr(result));
		result = DOCA_ERROR_NO_MEMORY;
		goto destroy_resources;
	}
	DOCA_LOG_INFO("Allocated dst buffer size: %ld", max_output_size);

	/* start measuring after context is online but before memory alloc */
	struct timespec start_time, end_time_memory, end_time_task, end_time;
	clock_gettime(CLOCK_MONOTONIC, &start_time);

	result = doca_mmap_set_memrange(state->dst_mmap, dst_buffer, max_output_size);
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
	result = doca_buf_set_data(src_doca_buf, file_data, file_size);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to set data in the DOCA buffer representing source buffer: %s",
			     doca_error_get_descr(result));
		goto destroy_dst_buf;
	}

	if (zlib_compatible) {
		/* Set data pointer to reserve space for the header */
		result = doca_buf_set_data(dst_doca_buf, dst_buffer + ZLIB_HEADER_SIZE, 0);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to set data in the DOCA buffer representing destination buffer: %s",
				     doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	}
	
	// clock_gettime(CLOCK_MONOTONIC, &end_time_memory);

	if (cfg->output_checksum || zlib_compatible) {
		result = submit_compress_deflate_task(&resources, src_doca_buf, dst_doca_buf, &output_checksum, &end_time_memory, &end_time_task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Compress task failed: %s", doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	} else {
		result = submit_compress_deflate_task(&resources, src_doca_buf, dst_doca_buf, NULL, &end_time_memory, &end_time_task);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Compress task failed: %s", doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
	}

	// clock_gettime(CLOCK_MONOTONIC, &end_time_task);

	result = doca_buf_get_data_len(dst_doca_buf, &data_len);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Unable to get data length in the DOCA buffer representing destination buffer: %s",
			     doca_error_get_descr(result));
		goto destroy_dst_buf;
	}
	write_len = data_len;

	clock_gettime(CLOCK_MONOTONIC, &end_time);
	double total_execution_time = timespec_diff_sec(&end_time, &start_time);
	double task_only_execution_time = timespec_diff_sec(&end_time_task, &end_time_memory);
	double memory_only_execution_time = timespec_diff_sec(&end_time, &end_time_task) + timespec_diff_sec(&end_time_memory, &start_time);
	double task_with_memory_init_execution_time = timespec_diff_sec(&end_time_task, &start_time);

	DOCA_LOG_INFO("Compressed file size: %ld", write_len);
	DOCA_LOG_INFO("Compression time: %.9f seconds", total_execution_time);
	DOCA_LOG_INFO("Task time: %.9f seconds", task_only_execution_time);
	DOCA_LOG_INFO("Memory time: %.9f seconds", memory_only_execution_time);
	DOCA_LOG_INFO("Task with Memory init time: %.9f seconds", task_with_memory_init_execution_time);
	DOCA_LOG_INFO("File compressed");

	if (zlib_compatible) {
		/* Write the Zlib header in the reserved header space */
		init_compress_zlib_header((struct compress_zlib_header *)dst_buffer);

		/* Retrieve the data address and compute the end of the data section */
		result = doca_buf_get_data(dst_doca_buf, &dst_buf_data);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to get data length in the DOCA buffer representing destination buffer: %s",
				     doca_error_get_descr(result));
			goto destroy_dst_buf;
		}
		dst_buf_tail = ((uint8_t *)dst_buf_data + data_len);

		/* Set data pointer to consider the added checksum */
		data_len += ZLIB_TRAILER_SIZE;
		result = doca_buf_set_data(dst_doca_buf, dst_buf_data, data_len);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Unable to set data in the DOCA buffer representing destination buffer: %s",
				     doca_error_get_descr(result));
			goto destroy_dst_buf;
		}

		/* Extract the Adler32 checksum from the output_checksum and write it after the compressed data */
		adler_checksum = (uint32_t)(output_checksum >> ADLER_CHECKSUM_SHIFT);
		be_adler_checksum = htobe32(adler_checksum);

		memcpy(dst_buf_tail, &be_adler_checksum, ZLIB_TRAILER_SIZE);

		/* Consider the Zlib header and the added checksum at the end */
		write_len += ZLIB_COMPATIBILITY_ADDITIONAL_MEMORY;
	}

	/* Write the result to output file */
	written_len = fwrite(dst_buffer, sizeof(uint8_t), write_len, out_file);
	if (written_len != write_len) {
		DOCA_LOG_ERR("Failed to write the DOCA buffer representing destination buffer into a file");
		result = DOCA_ERROR_OPERATING_SYSTEM;
		goto destroy_dst_buf;
	}

	DOCA_LOG_INFO("File was compressed successfully and saved in: %s", cfg->output_path);
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
