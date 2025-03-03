#include <re2/re2.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

std::pair<std::vector<std::string>, size_t> prepareAccidentDescrInMemory() {
    std::vector<std::string> data_lines;
    size_t total_size_bytes = 0;

    std::ifstream data_file("data/US_Accidents_Dec21_updated.csv");
    if (!data_file.is_open()) {
        std::cerr << "Could not open data file" << std::endl;
        return {data_lines, total_size_bytes};
    }

    std::string current_line;
    while (getline(data_file, current_line)) {
        // clean up current line
        current_line.erase(std::remove(current_line.begin(), current_line.end(), '\r'), current_line.end());
        
        std::stringstream data_stream(current_line);
        std::string s;
        int comma_idx = 0;
        while (getline(data_stream, s, ',')) {
            if (comma_idx++ == 9) {  // keep only description column
                total_size_bytes += s.size();
                data_lines.push_back(s);
                break;
            }
        }
    }
    data_file.close();
    data_lines.erase(data_lines.begin()); // remove header
    return {data_lines, total_size_bytes};
}

std::pair<std::vector<double>, std::vector<double>> benchmarkRegexes(const std::vector<std::unique_ptr<RE2>>& regexes, 
                                                                     std::vector<std::string>& clean_lines, int iters) {
    std::string sm;
    std::vector<double> full_match_durations;
    for (const auto &regex : regexes) {
        double avg_duration = 0;
        for (auto iter_idx = 0; iter_idx < iters; ++iter_idx) {
            auto start = std::chrono::high_resolution_clock::now();
            for (const auto &line : clean_lines) {
                RE2::FullMatch(line, *regex, &sm);
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
            avg_duration += seconds.count();
        }
        full_match_durations.emplace_back(avg_duration / iters);
    }

    std::vector<double> partial_match_durations;
    for (const auto &regex : regexes) {
        double avg_duration = 0;
        for (auto iter_idx = 0; iter_idx < iters; ++iter_idx) {
            auto start = std::chrono::high_resolution_clock::now();
            for (const auto &line : clean_lines) {
                RE2::PartialMatch(line, *regex, &sm);
            }
            auto end = std::chrono::high_resolution_clock::now();
            auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
            avg_duration += seconds.count();
        }
        partial_match_durations.emplace_back(avg_duration / iters);
    }

    assert(full_match_durations.size() == partial_match_durations.size());
    return std::pair<std::vector<double>, std::vector<double>>(full_match_durations, partial_match_durations);
}

int main(int argc, char** argv) {
    if(argc < 2) {
        std::cerr << "Usage: " << argv[0] << " DEVICE" << std::endl; 
        return EXIT_FAILURE;
    }

    // 1. prepare regexes
    std::vector<std::string> patterns = {
        "At (.+)Exit (.+)",
        "(.+) on (.+) at Exit (.+)",
        "on (.+) at (.+)",
        "Ramp to (.+)"
    };

    // Vector of unique_ptr<RE2> instead of RE2
    std::vector<std::unique_ptr<RE2>> precompiled_patterns;
    precompiled_patterns.reserve(patterns.size()); // Reserves pointer space

    for (const auto &pattern : patterns) {
        auto rePtr = std::make_unique<RE2>(pattern);
        if (!rePtr->ok()) {
            std::cerr << "Failed to compile pattern: " << pattern << "\n";
            return EXIT_FAILURE;
        }
        precompiled_patterns.push_back(std::move(rePtr));
    }

    // 2. get data from file and put in memory
    auto [lines, size] = prepareAccidentDescrInMemory();
    if (!lines.size() || !size) {
        std::cout << "Couldn't load lines properly" << std::endl;
        return EXIT_FAILURE;
    }

    // 3. benchmark full match
    int iters = 1;
    auto [full_match_durations, partial_match_durations] = benchmarkRegexes(precompiled_patterns, lines, iters);

    // 4. calculate throughput (size / duration)
    std::string header = "query_id (string),device (str),full (mib/s),partial (mib/s)";
    std::cout << header << std::endl;
    std::string row;
    double full_tput, part_tput;
    for (int idx = 0; idx < full_match_durations.size(); ++idx) {
        std::string row = "q";
        row.append(std::to_string(idx + 1)).append(",").append(argv[1]).append(",");
        full_tput = size / full_match_durations[idx] / 1048576.0;
        part_tput = size / partial_match_durations[idx] / 1048576.0;
        row.append(std::to_string(full_tput)).append(",");
        row.append(std::to_string(part_tput));
        std::cout << row << std::endl;
    }

    return EXIT_SUCCESS;
}