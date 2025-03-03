#include "re2_pipe.hpp"
#include <re2/re2.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

// Constructor: initialize members.
Re2Pipe::Re2Pipe(const std::string& input_location)
    : input_location_(input_location), iters_(3), total_size_bytes_(0) {
}

// init: Precompile regex patterns and load file data.
void Re2Pipe::init() {
    // Precompile regex patterns.
    patterns_ = {"At (.+)Exit (.+)",
                 "(.+) on (.+) at Exit (.+)",
                 "on (.+) at (.+)",
                 "Ramp to (.+)"};
    for (const auto &pattern : patterns_) {
        auto re_ptr = std::make_unique<RE2>(pattern);
        if (!re_ptr->ok()) {
            throw std::runtime_error("Failed to compile pattern: " + pattern);
        }
        regexes_.push_back(std::move(re_ptr));
    }

    // Load and prepare file data.
    std::ifstream data_file(input_location_);
    if (!data_file.is_open()) {
        throw std::runtime_error("Could not open data file");
    }
    std::string current_line;
    while (std::getline(data_file, current_line)) {
        // Remove carriage return characters.
        current_line.erase(std::remove(current_line.begin(), current_line.end(), '\r'), current_line.end());
        std::stringstream data_stream(current_line);
        std::string token;
        int comma_idx = 0;
        while (std::getline(data_stream, token, ',')) {
            if (comma_idx++ == 9) { // Extract the description column.
                total_size_bytes_ += token.size();
                lines_.push_back(token);
                break;
            }
        }
    }
    data_file.close();
    // Remove header line if present.
    if (!lines_.empty()) {
        lines_.erase(lines_.begin());
    }
}

// execute: Benchmark regexes using full and partial match methods.
void Re2Pipe::execute() {
    std::string dummy;
    // Benchmark full match durations.
    std::cerr << "CPU regex starting iters..." << std::endl;
    for (const auto &regex : regexes_) {
        double avg_duration = 0.0;
        for (int iter = 0; iter < iters_; ++iter) {
            auto start = std::chrono::high_resolution_clock::now();
            for (const auto &line : lines_) {
                RE2::FullMatch(line, *regex, &dummy);
            }
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> duration = end - start;
            avg_duration += duration.count();
        }
        full_match_durations_.push_back(avg_duration / iters_);
    }
}

// cleanup: Output the benchmark results.
void Re2Pipe::cleanup() {
    std::cout << "query_id (string),device (str),full (mib/s)" << std::endl;
    for (size_t idx = 0; idx < full_match_durations_.size(); ++idx) {
        double full_tput = total_size_bytes_ / full_match_durations_[idx] / 1048576.0;
        std::cout << "q" << (idx + 1) << "," << "cpu_re2" << ","
                  << full_tput << std::endl;
    }
}
