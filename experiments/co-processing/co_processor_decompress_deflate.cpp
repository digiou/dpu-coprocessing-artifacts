#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <vector>
#include <unistd.h>

#include <doca_buf_inventory.h>
#include <doca_compress.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include "doca_consumer.hpp"
#include "simple_barrier.hpp"
#include "zpipe.hpp"
#include "doca_decompress_deflate.hpp"

#include <nlohmann/json.hpp>

void pin_and_expose(const char* name, int core) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);
    pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);

    pid_t tid = syscall(SYS_gettid);
    printf("%s TID=%d (core %d)\n", name, tid, core);
    fflush(stdout);
}

double thread_cpu_seconds() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

std::string calculateSeconds(const std::chrono::steady_clock::time_point end,
											   const std::chrono::steady_clock::time_point start) {
	auto elapsed = end - start;
	auto seconds = std::chrono::duration<double>(elapsed).count();
	// Convert the float to a string with fixed formatting and desired precision
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(8) << seconds;
	std::string formattedValue = oss.str();
	return formattedValue;
}

void docaWriteJson(const std::vector<std::string> times, const std::string filename) {
	nlohmann::json j;
	std::vector<std::string> keys = {"overall_submission_elapsed", "task_submission_elapsed",
									 "busy_wait_elapsed", "cb_elapsed", "cb_end_elapsed", 
									 "ctx_stop_elapsed", "cpu_time_elapsed", 
									 "joined_submission_elapsed"};
	for (uint16_t idx = 0; idx < times.size(); ++idx) {
		j[keys[idx]] = times[idx];
	}

	// Pretty print the JSON with an indent of 4 spaces
    std::string prettyJson = j.dump(4);

	// Optionally, write the pretty printed JSON to a file
    std::ofstream outFile(filename);
    if (outFile) {
        outFile << prettyJson;
    }
}

void cpuWriteJson(const std::vector<std::string> times, const std::string filename) {
	nlohmann::json j;
	std::vector<std::string> keys = {"overall_submission_elapsed", "cpu_time_elapsed",
									 "joined_submission_elapsed"};
	for (uint16_t idx = 0; idx < times.size(); ++idx) {
		j[keys[idx]] = times[idx];
	}

	// Pretty print the JSON with an indent of 4 spaces
    std::string prettyJson = j.dump(4);

	// Optionally, write the pretty printed JSON to a file
    std::ofstream outFile(filename);
    if (outFile) {
        outFile << prettyJson;
    }
}

void doca_decompress_deflate_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier, 
		uint64_t asked_buffer_size, uint64_t asked_num_buffers, size_t original_filesize, int bf_version) {
	// pin thread to specific core
	pin_and_expose("DPU", 4);  // pick any isolated core
	
	// determine version
	DecompressDeflateConsumer::DEVICE_TYPE device = DecompressDeflateConsumer::DEVICE_TYPE::BF2; 
	if (bf_version == 3) {
		device = DecompressDeflateConsumer::DEVICE_TYPE::BF3;
	}

	// DOCA init
	auto consumer_decompress_deflate = DecompressDeflateConsumer(device, asked_buffer_size, asked_num_buffers, original_filesize, true);

	// log waiting state
	std::cout << "DOCA Decompress ready, waiting..." << std::endl;

	// wait for sync
	start_barrier.arrive_and_wait();

	// log processing state
	std::cout << "DOCA Decompress start processing..." << std::endl;

	// entered processing
	auto processing_start = std::chrono::steady_clock::now();

	// TODO: send task
	consumer_decompress_deflate.executeDocaTask();

	// wait for sync
	end_barrier.arrive_and_wait();

	// both HW finished processing
	auto processing_end = std::chrono::steady_clock::now();

	// log writing state
	std::cout << "DOCA Decompress results..." << std::endl;

	// write results and output
	auto result_times = consumer_decompress_deflate.getDocaResults();
	result_times.emplace_back(calculateSeconds(processing_end, processing_start));
	auto name = "results-" + consumer_decompress_deflate.getName() + ".json";
	docaWriteJson(result_times, name);
	printf("[DOCA] user+sys = %s s\n", result_times[6].c_str());
}

void cpu_inflate_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
	// pin thread to specific core
	pin_and_expose("CPU", 3);  // pick any isolated core
	
	// CPU init
	Zpipe zpipe;
	// bool singleBufferExecution = true;
	auto ret = zpipe.deflate_init("/dev/shm/infl", "/dev/shm/infl-input");
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}
	ret = zpipe.deflate_execute_single_buffer();
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}
	zpipe.deflate_cleanup();

	ret = zpipe.inflate_init("/dev/shm/infl-input", "/dev/shm/infl-out");
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}

	// log waiting state
    std::cout << "CPU ready, waiting..." << std::endl;

	// wait for sync
	start_barrier.arrive_and_wait();

	// log cpu-time start
	double cpu_time_start = thread_cpu_seconds();

	// entered processing
	auto processing_start = std::chrono::steady_clock::now();

	// log processing state
    std::cout << "CPU start processing..." << std::endl;

	// process data
	ret = zpipe.inflate_execute_single_buffer();
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}

	// cpu finished its task
	auto cpu_task_end = std::chrono::steady_clock::now();

	// log cpu-time end
	double cpu_time_end = thread_cpu_seconds();

	// wait for sync
	end_barrier.arrive_and_wait();

	// both HW finished processing
	auto processing_end = std::chrono::steady_clock::now();

	// log processing state
	std::cout << "CPU get results..." << std::endl;

	zpipe.inflate_cleanup();

	auto cpu_time_elapsed = cpu_time_end - cpu_time_start;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8) << cpu_time_elapsed;
    std::string thread_time_elapsed = oss.str();

	std::vector<std::string> results{calculateSeconds(cpu_task_end, processing_start),
						thread_time_elapsed, calculateSeconds(processing_end, processing_start)};
	cpuWriteJson(results, "results-cpu-decompress-deflate.json");
	printf("[CPU] user+sys = %s s\n", thread_time_elapsed.c_str());
}

int main(int argc, char **argv) {
	// Ensure we receive exactly two arguments
    if (argc != 7) {
        std::cerr << "Usage: " << argv[0] << " <percentage1> <percentage2> <original_filesize> <bf_version> <asked_buffer_size> <asked_num_buffers>" << std::endl;
        return 1;
    }

	// Convert arguments to integers
    int percentage_cpu = std::stoi(argv[1]);
    int percentage_dpu = std::stoi(argv[2]);
	size_t original_filesize = static_cast<size_t>(std::stoull(argv[3]));
	int bf_version = std::stoi(argv[4]);
	uint64_t asked_buffer_size = std::stoi(argv[5]);
	uint64_t asked_num_buffers = std::stoi(argv[6]);

    // Validate percentage range
    if (percentage_cpu < 0 || percentage_cpu > 100 || percentage_dpu < 0 || percentage_dpu > 100) {
        std::cerr << "Error: Percentages must be between 0 and 100." << std::endl;
        return 1;
    }

	if (bf_version != 3 && bf_version != 2) {
		std::cerr << "Error: device should be (2|3)." << std::endl;
        return 1;
	}

	if (asked_buffer_size == 0 || asked_num_buffers == 0) {
		std::cerr << "Error: asked_buffer_size or asked_num_buffers should not be 0." << std::endl;
        return 1;
	}


	// how many threads to use
	int THREAD_COUNT = 2;
	if (percentage_cpu == 0 || percentage_dpu == 0) {
		THREAD_COUNT = 1;
	}

	// create sync barriers
	SimpleBarrier start_barrier(THREAD_COUNT);
	SimpleBarrier end_barrier(THREAD_COUNT);

	// workers
	std::vector<std::thread> threads;
	threads.reserve(THREAD_COUNT);
	
	// Decompress DEFLATE co-processing
	if (percentage_cpu > 0) {
		threads.emplace_back(cpu_inflate_worker, std::ref(start_barrier), std::ref(end_barrier));
	}
	
	if (percentage_dpu > 0) {
		threads.emplace_back(doca_decompress_deflate_worker, std::ref(start_barrier), 
							 std::ref(end_barrier),
							 std::ref(asked_buffer_size),
							 std::ref(asked_num_buffers),
							 std::ref(original_filesize), 
							 std::ref(bf_version));
	}

	// Join threads
    for (auto& t : threads) {
        t.join();
    }

	std::cout << "Both threads done" << std::endl;

    return EXIT_SUCCESS;
}