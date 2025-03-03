#ifndef KAYON_REGEX_PIPE_H
#define KAYON_REGEX_PIPE_H

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <re2/re2.h>

class Re2Pipe {
public:
    // Constructor takes the device identifier.
    explicit Re2Pipe(const std::string& file_location);

    // Initialization: precompile regexes and load file data into memory.
    void init();

    // Execute processing: benchmark the regexes.
    void execute();

    // Cleanup: output the results.
    void cleanup();

private:
    int iters_;
    size_t total_size_bytes_;
    std::vector<std::string> patterns_;
    std::vector<std::unique_ptr<RE2>> regexes_;
    std::vector<std::string> lines_;
    std::vector<double> full_match_durations_;
    std::string input_location_;
};

#endif // KAYON_REGEX_BENCHMARK_H
