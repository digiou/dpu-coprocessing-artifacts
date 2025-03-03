#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

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
#include "re2_pipe.hpp"

#include <nlohmann/json.hpp>

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
									 "ctx_stop_elapsed", "joined_submission_elapsed"};
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
	std::vector<std::string> keys = {"overall_submission_elapsed", "joined_submission_elapsed"};
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

// void doca_regex_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier, 
// 			uint64_t asked_buffer_size, uint64_t asked_num_buffers, size_t original_filesize) {

// 	// DOCA init
// 	auto consumer_decompress_lz4 = DecompressLz4Consumer(DecompressLz4Consumer::DEVICE_TYPE::BF3, asked_buffer_size, asked_num_buffers, original_filesize, true);

// 	// log waiting state
// 	std::cout << "DOCA Decompress ready, waiting..." << std::endl;

// 	// wait for sync
// 	start_barrier.arrive_and_wait();

// 	// log processing state
// 	std::cout << "DOCA Decompress start processing..." << std::endl;

// 	// entered processing
// 	auto processing_start = std::chrono::steady_clock::now();

// 	// TODO: send task
// 	consumer_decompress_lz4.executeDocaTask();

// 	// wait for sync
// 	end_barrier.arrive_and_wait();

// 	// both HW finished processing
// 	auto processing_end = std::chrono::steady_clock::now();

// 	// log writing state
// 	std::cout << "DOCA Decompress results..." << std::endl;

// 	// write results and output
// 	auto result_times = consumer_decompress_lz4.getDocaResults();
// 	result_times.emplace_back(calculateSeconds(processing_end, processing_start));
// 	auto name = "results-" + consumer_decompress_lz4.getName() + ".json";
// 	docaWriteJson(result_times, name);
// }

void cpu_regex_decompress_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
	// CPU init
	Re2Pipe re2_pipe{"/dev/shm/cpu-regex"};
	re2_pipe.init();

	// log waiting state
    std::cout << "CPU ready, waiting..." << std::endl;

	// wait for sync
	start_barrier.arrive_and_wait();

	// entered processing
	auto processing_start = std::chrono::steady_clock::now();

	// log processing state
    std::cerr << "CPU regex start processing..." << std::endl;

	// process data
	re2_pipe.execute();

	// cpu finished its task
	auto cpu_task_end = std::chrono::steady_clock::now();

	// log processing state
	std::cerr << "CPU regex end processing!" << std::endl;

	// wait for sync
	end_barrier.arrive_and_wait();

	// both HW finished processing
	auto processing_end = std::chrono::steady_clock::now();

	// log processing state
	std::cerr << "CPU regex get results..." << std::endl;

	re2_pipe.cleanup();

	std::vector<std::string> results{calculateSeconds(cpu_task_end, processing_start), calculateSeconds(processing_end, processing_start)};
	cpuWriteJson(results, "results-cpu-regex.json");
}

int main(int argc, char **argv) {
	// Ensure we receive exactly two arguments
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <percentage1> <percentage2> <original_filesize>" << std::endl;
        return 1;
    }

	// Convert arguments to integers
    int percentage_cpu = std::stoi(argv[1]);
    int percentage_dpu = std::stoi(argv[2]);
	int original_filesize = std::stoi(argv[3]);

    // Validate percentage range
    if (percentage_cpu < 0 || percentage_cpu > 100 || percentage_dpu < 0 || percentage_dpu > 100) {
        std::cerr << "Error: Percentages must be between 0 and 100." << std::endl;
        return 1;
    }

	// how many threads to use
	int THREAD_COUNT = 1;
	if (percentage_cpu == 0 || percentage_dpu == 0) {
		THREAD_COUNT = 1;
	}

	// create sync barriers
	SimpleBarrier start_barrier(THREAD_COUNT);
	SimpleBarrier end_barrier(THREAD_COUNT);

	// workers
	std::vector<std::thread> threads;
	threads.reserve(THREAD_COUNT);
	
	// Decompress LZ4 co-processing
	// if (percentage_cpu > 0) {
	// 	threads.emplace_back(cpu_regex_decompress_worker, std::ref(start_barrier), std::ref(end_barrier));
	// }
	threads.emplace_back(cpu_regex_decompress_worker, std::ref(start_barrier), std::ref(end_barrier));
	
	// if (percentage_dpu > 0) {
	// 	threads.emplace_back(doca_regex_worker, std::ref(start_barrier), 
	// 						 std::ref(end_barrier),
	// 						 std::ref(asked_buffer_size),
	// 						 std::ref(asked_num_buffers),
	// 						 std::ref(original_filesize));
	// }

	// Join threads
    for (auto& t : threads) {
        t.join();
    }

	std::cout << "Both threads done" << std::endl;

    return EXIT_SUCCESS;
}