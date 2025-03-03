#ifndef ZPIPE_H
#define ZPIPE_H
#define SET_BINARY_MODE(file)
#include <iostream>
#include <cstring>
#include <cassert>
#include <vector>
#include "zlib.h"

class Zpipe {
  public:
    Zpipe();

    // 1) Init: prepare single buffer (or multiple chunks otherwise) from disk
    int deflate_init(const std::string &inFilename, const std::string &outFilename, bool singleBufferExecution = true); // compress init
    int inflate_init(const std::string &inFilename, const std::string &outFilename, bool singleBufferExecution = true); // decompress init

    // 2) Execution: (de)compress the in-memory data into the output file.
    int deflate_execute();
    int deflate_execute_single_buffer();
    int inflate_execute_single_buffer();

    // 3) Cleanup: finalize/close z_stream, close files, reset state.
    void deflate_cleanup();
    void inflate_cleanup();

    // reference file-to-chunk-to-file impl from https://terminalroot.com/how-to-use-the-zlib-library-with-cpp/
    int def( FILE *, FILE *, int ); // compress
    int inf( FILE *, FILE * ); // decompress
    void zerr( int );
  private:
    static const size_t CHUNK = 16384;
    bool singleBufferExecution = false;

    // handle init internally
    int m_init(const std::string &inFilename, const std::string &outFilename, bool inflate,
               bool singleBufferExecution = false);

    // handle cleanup internally
    void m_cleanup(bool inflate);

    // Helper for reading the file in CHUNK_SIZE increments
    int readFileInChunks();
    // Helper to read the file in a single large buffer
    int readFileFully();

    // Memory chunks that hold the entire input file
    std::vector<std::vector<unsigned char>> m_inputChunks;

    // Data for compressed chunks (the entire compressed output)
    std::vector<std::vector<unsigned char>> m_compressedChunks;

    // For single-buffer approach
    std::vector<unsigned char> m_fullInput;        // entire file in one buffer
    std::vector<unsigned char> m_fullOutput;   // entire resulting (compressed/decompressed) data

    FILE* m_inFile;
    FILE* m_outFile;
    z_stream stream;
    int m_deflateLevel;
};
#endif