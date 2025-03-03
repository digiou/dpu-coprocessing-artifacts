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
#include "zpipe.hpp"
#include "lz4_pipe.hpp"
#include "doca_compress.hpp"
// #include "doca_decompress_deflate.hpp"
// #include "doca_decompress_lz4.hpp"

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

void doca_compress_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
	// DOCA init
	auto consumer_compress_deflate = CompressConsumer(CompressConsumer::DEVICE_TYPE::BF2, 1, true);

	// wait for sync
	start_barrier.arrive_and_wait();

	// log processing state
	std::cout << "DOCA Compress start processing..." << std::endl;

	// entered processing
	auto processing_start = std::chrono::steady_clock::now();

	// execute task
	consumer_compress_deflate.executeDocaTask();

	// wait for sync
	end_barrier.arrive_and_wait();

	// both HW finished processing
	auto processing_end = std::chrono::steady_clock::now();

	// log writing state
	std::cout << "DOCA Compress results..." << std::endl;

	// write results and output
	auto result_times = consumer_compress_deflate.getDocaResults();
	result_times.emplace_back(calculateSeconds(processing_end, processing_start));
	auto name = "results-" + consumer_compress_deflate.getName() + ".json";
	docaWriteJson(result_times, name);
}

// void doca_decompress_deflate_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
// 	// DOCA init
// 	auto consumer_decompress_deflate = DecompressDeflateConsumer(DecompressDeflateConsumer::DEVICE_TYPE::BF2, 1, 1, 1, true);

// 	// log waiting state
// 	std::cout << "DOCA Decompress ready, waiting..." << std::endl;

// 	// wait for sync
// 	start_barrier.arrive_and_wait();

// 	// log processing state
// 	std::cout << "DOCA Decompress start processing..." << std::endl;

// 	// TODO: send task
// 	consumer_decompress_deflate.executeDocaTask();

// 	// wait for sync
// 	end_barrier.arrive_and_wait();

// 	// log writing state
// 	std::cout << "DOCA Decompress results..." << std::endl;

// 	// write results and output
// 	auto result_times = consumer_decompress_deflate.writeDocaResults();
// 	auto name = "results-" + consumer_decompress_deflate.getName() + ".json";
// 	writeJson(result_times, name);

// 	// log end processing state
//     std::cout << "DOCA Decompress wrote results" << std::endl;
// }

// void doca_decompress_lz4_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
// 	// DOCA init
// 	auto consumer_decompress_lz4 = DecompressLz4Consumer(DecompressLz4Consumer::DEVICE_TYPE::BF2, 1, 1, 1, true);

// 	// log waiting state
// 	std::cout << "DOCA Decompress ready, waiting..." << std::endl;

// 	// wait for sync
// 	start_barrier.arrive_and_wait();

// 	// log processing state
// 	std::cout << "DOCA Decompress start processing..." << std::endl;

// 	// TODO: send task
// 	consumer_decompress_lz4.executeDocaTask();

// 	// wait for sync
// 	end_barrier.arrive_and_wait();

// 	// log writing state
// 	std::cout << "DOCA Decompress results..." << std::endl;

// 	// write results and output
// 	auto result_times = consumer_decompress_lz4.writeDocaResults();
// 	auto name = "results-" + consumer_decompress_lz4.getName() + ".json";
// 	writeJson(result_times, name);

// 	// log end processing state
//     std::cout << "DOCA Decompress wrote results" << std::endl;
// }

void cpu_deflate_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
	// CPU init
	Zpipe zpipe;
	auto ret = zpipe.deflate_init("/dev/shm/deflt-input", "/dev/shm/deflt-out");
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}

	// wait for sync
	start_barrier.arrive_and_wait();

	// entered processing
	auto processing_start = std::chrono::steady_clock::now();

	// log processing state
    std::cout << "CPU dflt start processing..." << std::endl;

	// process data
	ret = zpipe.deflate_execute_single_buffer();
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}

	// cpu finished its task
	auto cpu_task_end = std::chrono::steady_clock::now();

	// log processing state
	std::cout << "CPU dflt end processing!" << std::endl;

	// wait for sync
	end_barrier.arrive_and_wait();

	// both HW finished processing
	auto processing_end = std::chrono::steady_clock::now();

	// log processing state
	std::cout << "CPU dflt get results..." << std::endl;

	zpipe.deflate_cleanup();

	std::vector<std::string> results{calculateSeconds(cpu_task_end, processing_start), calculateSeconds(processing_end, processing_start)};
	cpuWriteJson(results, "results-cpu-compress.json");
}

void cpu_inflate_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
	// CPU init
	Zpipe zpipe;
	// bool singleBufferExecution = true;
	auto ret = zpipe.inflate_init("/tmp/infl-input", "/tmp/infl-out");
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}

	// log waiting state
    std::cout << "CPU ready, waiting..." << std::endl;

	// wait for sync
	start_barrier.arrive_and_wait();

	// log processing state
    std::cout << "CPU start processing..." << std::endl;

	// process data
	ret = zpipe.inflate_execute_single_buffer();
	if (ret != Z_OK){
		zpipe.zerr(ret);
	}

	// log processing state
	std::cout << "CPU end processing!" << std::endl;

	// wait for sync
	end_barrier.arrive_and_wait();

	// log processing state
	std::cout << "CPU write results..." << std::endl;

	zpipe.inflate_cleanup();

	// log end processing state
	std::cout << "CPU wrote results" << std::endl;
}

void cpu_lz4_decompress_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
	// CPU init
	LZ4Pipe lz4_pipe;
	auto ret = lz4_pipe.decompress_init("/tmp/lz4-input", "/tmp/lz4-output");

	// log waiting state
    std::cout << "CPU ready, waiting..." << std::endl;

	// wait for sync
	start_barrier.arrive_and_wait();

	// log processing state
    std::cout << "CPU start processing..." << std::endl;

	// process data
	ret = lz4_pipe.decompress_execute();

	// log processing state
	std::cout << "CPU end processing!" << std::endl;

	// wait for sync
	end_barrier.arrive_and_wait();

	// log processing state
	std::cout << "CPU write results..." << std::endl;

	lz4_pipe.decompress_cleanup();

	// log end processing state
	std::cout << "CPU wrote results" << std::endl;
}

void cpu_lz4_compress_worker(SimpleBarrier& start_barrier, SimpleBarrier& end_barrier) {
	// CPU init
	LZ4Pipe lz4_pipe;
	auto ret = lz4_pipe.compress_init("/tmp/lz4-input", "/tmp/lz4-output");

	// log waiting state
    std::cout << "CPU ready, waiting..." << std::endl;

	// wait for sync
	start_barrier.arrive_and_wait();

	// log processing state
    std::cout << "CPU start processing..." << std::endl;

	// process data
	ret = lz4_pipe.compress_execute();

	// log processing state
	std::cout << "CPU end processing!" << std::endl;

	// wait for sync
	end_barrier.arrive_and_wait();

	// log processing state
	std::cout << "CPU write results..." << std::endl;

	lz4_pipe.compress_cleanup();

	// log end processing state
	std::cout << "CPU wrote results" << std::endl;
}

int main(int argc, char **argv) {
	// Ensure we receive exactly two arguments
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <percentage1> <percentage2>\n";
        return 1;
    }

	// Convert arguments to integers
    int percentage_cpu = std::stoi(argv[1]);
    int percentage_dpu = std::stoi(argv[2]);

    // Validate percentage range
    if (percentage_cpu < 0 || percentage_cpu > 100 || percentage_dpu < 0 || percentage_dpu > 100) {
        std::cerr << "Error: Percentages must be between 0 and 100." << std::endl;
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
	
	// Compress co-processing
	if (percentage_cpu > 0) {
		threads.emplace_back(cpu_deflate_worker, std::ref(start_barrier), std::ref(end_barrier));
	}
	
	if (percentage_dpu > 0) {
		threads.emplace_back(doca_compress_worker, std::ref(start_barrier), std::ref(end_barrier));
	}

	// Decompress DEFLATE co-processing
	// threads.emplace_back(cpu_inflate_worker, std::ref(start_barrier), std::ref(end_barrier));
	// threads.emplace_back(doca_decompress_deflate_worker, std::ref(start_barrier), std::ref(end_barrier));
	
	// Decompress LZ4 co-processing
	// threads.emplace_back(cpu_lz4_decompress_worker, std::ref(start_barrier), std::ref(end_barrier));
	// threads.emplace_back(doca_decompress_lz4_worker, std::ref(start_barrier), std::ref(end_barrier));

	// Join threads
    for (auto& t : threads) {
        t.join();
    }

	std::cout << "Both threads done" << std::endl;

    return EXIT_SUCCESS;
}