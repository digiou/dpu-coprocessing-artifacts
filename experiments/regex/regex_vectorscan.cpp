#include <hs/hs.h>  // Hyperscan/vectorscan header
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>  // for std::remove

/*****************************************************************************
 * 1. Load CSV into memory, extracting "Description" column
 *****************************************************************************/
std::pair<std::vector<std::string>, size_t> prepareAccidentDescrInMemory() {
    std::vector<std::string> data_lines;
    size_t total_size_bytes = 0;

    std::ifstream data_file("data/US_Accidents_Dec21_updated.csv");
    if (!data_file.is_open()) {
        std::cerr << "Could not open data file" << std::endl;
        return {data_lines, total_size_bytes};
    }

    std::string current_line;
    while (std::getline(data_file, current_line)) {
        // Remove any trailing '\r'
        current_line.erase(std::remove(current_line.begin(), current_line.end(), '\r'),
                           current_line.end());
        
        // Parse CSV row
        std::stringstream data_stream(current_line);
        std::string s;
        int comma_idx = 0;
        // We want the 10th column (index 9) for "Description"
        while (std::getline(data_stream, s, ',')) {
            if (comma_idx++ == 9) {
                // We found the description column
                total_size_bytes += s.size();
                data_lines.push_back(s);
                break;
            }
        }
    }
    data_file.close();

    // Remove header row (the first line in many CSV files), if present
    if (!data_lines.empty()) {
        data_lines.erase(data_lines.begin());
    }

    return {data_lines, total_size_bytes};
}

/*****************************************************************************
 * 2. Helper to anchor patterns for "full match" scanning
 *****************************************************************************/
std::string makeAnchored(const std::string &pat) {
    // Hyperscan doesn't have a direct "FullMatch" concept like RE2,
    // so we anchor with ^ and $ to simulate "must match entire string".
    return "^" + pat + "$";
}

/*****************************************************************************
 * 3. Compile a set of patterns into a single Hyperscan/vectorscan database.
 *    - Each pattern gets an ID = its index in the vector.
 *****************************************************************************/
hs_database_t* compileDatabase(const std::vector<std::string> &patterns) {
    // Patterns, flags, IDs
    std::vector<const char*> cstr_patterns;
    std::vector<unsigned int> flags;
    std::vector<unsigned int> ids;

    cstr_patterns.reserve(patterns.size());
    flags.reserve(patterns.size());
    ids.reserve(patterns.size());

    for (unsigned i = 0; i < patterns.size(); ++i) {
        cstr_patterns.push_back(patterns[i].c_str());
        // HS_FLAG_SOM_LEFTMOST or HS_FLAG_DOTALL or HS_FLAG_CASELESS if needed, etc.
        flags.push_back(0); 
        ids.push_back(i);
    }

    hs_database_t *database = nullptr;
    hs_compile_error_t *compileErr = nullptr;

    // Block (single-shot) mode for scanning each line individually
    hs_error_t err = hs_compile_multi(
        cstr_patterns.data(),
        flags.data(),
        ids.data(),
        (unsigned int)patterns.size(),
        HS_MODE_BLOCK,
        nullptr, // No platform tuning
        &database,
        &compileErr
    );

    if (err != HS_SUCCESS) {
        if (compileErr) {
            std::cerr << "ERROR: " << compileErr->message << std::endl;
            hs_free_compile_error(compileErr);
        }
        return nullptr;
    }
    hs_free_compile_error(compileErr);

    return database;
}

/*****************************************************************************
 * 4. Utility: Allocates scratch for the given database.
 *****************************************************************************/
hs_scratch_t* allocScratch(hs_database_t *db) {
    hs_scratch_t* scratch = nullptr;
    if (hs_alloc_scratch(db, &scratch) != HS_SUCCESS) {
        std::cerr << "ERROR: Unable to allocate scratch space." << std::endl;
        return nullptr;
    }
    return scratch;
}

/*****************************************************************************
 * 5. Callback function for each match event
 *    - We'll just note that a match occurred. If we wanted multiple matches
 *      or capturing, we'd do something more elaborate. 
 *****************************************************************************/
static int onMatch(unsigned int id,
                   unsigned long long from,
                   unsigned long long to,
                   unsigned int flags,
                   void *context)
{
    bool *matched = (bool*)context;
    *matched = true; 
    // Return non-zero to abort scanning after the first match if you only
    // care about "match or no match."
    return 1; 
}

/*****************************************************************************
 * 6. Benchmark scanning a set of patterns on all lines, repeated "iters" times.
 *    We'll treat "dbPartial" as the unanchored patterns, 
 *    and "dbFull" as the anchored patterns. 
 *****************************************************************************/
std::pair<std::vector<double>, std::vector<double>> 
benchmarkRegexes(hs_database_t* dbPartial,
                 hs_database_t* dbFull,
                 std::vector<std::string>& lines,
                 int iters)
{
    // 6a. Allocate scratch for partial DB
    hs_scratch_t* scratchPartial = allocScratch(dbPartial);
    if(!scratchPartial) {
        return {{}, {}};
    }
    // 6b. Allocate scratch for full DB
    hs_scratch_t* scratchFull = allocScratch(dbFull);
    if(!scratchFull) {
        hs_free_scratch(scratchPartial);
        return {{}, {}};
    }

    // We'll measure time for partial patterns:
    double partialTotalSec = 0.0;
    for (int i = 0; i < iters; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        // Scan each line
        for (const auto &line : lines) {
            bool matched = false;
            hs_scan(dbPartial,
                    line.data(),
                    line.size(),
                    0, // flags
                    scratchPartial,
                    onMatch,
                    &matched);
            // We don’t store "sm" like in RE2 code,
            // because Hyperscan doesn't provide captures by default.
        }
        auto end = std::chrono::high_resolution_clock::now();
        partialTotalSec += std::chrono::duration<double>(end - start).count();
    }
    double partialAvgSec = partialTotalSec / iters;

    // Next measure time for anchored (full) patterns:
    double fullTotalSec = 0.0;
    for (int i = 0; i < iters; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        // Scan each line
        for (const auto &line : lines) {
            bool matched = false;
            hs_scan(dbFull,
                    line.data(),
                    line.size(),
                    0,
                    scratchFull,
                    onMatch,
                    &matched);
        }
        auto end = std::chrono::high_resolution_clock::now();
        fullTotalSec += std::chrono::duration<double>(end - start).count();
    }
    double fullAvgSec = fullTotalSec / iters;

    // Clean up scratch
    hs_free_scratch(scratchPartial);
    hs_free_scratch(scratchFull);

    // Return average durations as a pair
    // (We only have one partial "set" and one full "set", so we return 
    // single-element "vectors" to match the shape of your original code.)
    return ( std::make_pair(
                    std::vector<double>{fullAvgSec},
                    std::vector<double>{partialAvgSec}
                )
            );
}

/*****************************************************************************
 * 7. main(): tie it all together
 *****************************************************************************/
int main(int argc, char** argv) {
    if(argc < 2) {
        std::cerr << "Usage: " << argv[0] << " DEVICE" << std::endl; 
        return EXIT_FAILURE;
    }

    // 1) Load lines from CSV file (Description column).
    auto [lines, size] = prepareAccidentDescrInMemory();
    if (lines.empty() || size == 0) {
        std::cerr << "No lines loaded, or file missing.\n";
        return EXIT_FAILURE;
    }

    // 2) Prepare partial (unanchored) and full (anchored) patterns:
    std::vector<std::string> partialPatterns = {
        "At (.+)Exit (.+)",
        "(.+) on (.+) at Exit (.+)",
        "on (.+) at (.+)",
        "Ramp to (.+)"
    };

    // 3) prepare for output before per-regex results
    std::string header = "query_id (string),device (str),full (mib/s),partial (mib/s)";
    std::cout << header << std::endl;
    std::string row;
    double full_tput, part_tput;
    int pattern_idx = 0;
    for (auto pattern : partialPatterns) { // do 1-1 since hs processes one-db-at-a-time
        std::vector<std::string> fullPatterns;
        fullPatterns.reserve(partialPatterns.size());
        for (const auto &p : partialPatterns) {
            fullPatterns.push_back(makeAnchored(p)); 
            // e.g. "At (.+)Exit (.+)" -> "^At (.+)Exit (.+)$"
        }

        // 3) Compile each set into a separate database
        hs_database_t *dbPartial = compileDatabase(partialPatterns);
        if(!dbPartial) {
            return EXIT_FAILURE;
        }
        hs_database_t *dbFull = compileDatabase(fullPatterns);
        if(!dbFull) {
            hs_free_database(dbPartial);
            return EXIT_FAILURE;
        }

        // 4) Benchmark scanning times
        int iters = 3; // adapt as needed
        auto [full_match_durations, partial_match_durations] = benchmarkRegexes(dbPartial, dbFull, lines, iters);

        // 5) calculate throughput (size / duration)
        std::string row = "q";
        row.append(std::to_string(pattern_idx + 1)).append(",").append(argv[1]).append(",");
        full_tput = size / full_match_durations[0] / 1048576.0;
        part_tput = size / partial_match_durations[0] / 1048576.0;
        row.append(std::to_string(full_tput)).append(",");
        row.append(std::to_string(part_tput));
        std::cout << row << std::endl;

        pattern_idx++;

        // 6) Cleanup
        hs_free_database(dbPartial);
        hs_free_database(dbFull);
    }

    

    return EXIT_SUCCESS;
}
