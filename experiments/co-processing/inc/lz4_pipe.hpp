#ifndef LZ4_PIPE_HPP
#define LZ4_PIPE_HPP

#include <string>
#include <vector>

class LZ4Pipe {
public:
    LZ4Pipe();
    ~LZ4Pipe();

    // 1) decompress_init: 
    //    - Reads input file (uncompressed) 
    //    - Compresses it in-memory (so we have a valid LZ4 buffer)
    int decompress_init(const std::string &inputFile, const std::string &outputFile);

    // 2) decompress_execute: 
    //    - Decompress the in-memory LZ4 buffer 
    //    - This is where you'll measure "pure decompression" time
    int decompress_execute();

    // 3) decompress_cleanup: 
    //    - Writes the *decompressed* data to disk, 
    //    - Clears buffers if desired
    void decompress_cleanup();

    // 1) init:
    //    - Reads input file (uncompressed)
    int compress_init(const std::string &inputFile, const std::string &outputFile);

    // 2) compress_execute:
    //    - Reads input file (uncompressed)
    //    - Compress the in-memory LZ4 buffer
    int compress_execute();

    // 3) compress_cleanup: 
    //    - Writes the *compressed* data to disk, 
    //    - Clears buffers if desired
    void compress_cleanup();
private:
    // Helper: read entire uncompressed file into m_originalData
    int readInputFile(const std::string &filename);

    // Helper: compress m_originalData -> m_compressedData
    int compressInMemory();

    // The original raw file data
    std::vector<char> m_originalData;

    // Compressed data (in LZ4 format)
    std::vector<char> m_compressedData;
    int m_compressedSize; // actual size after compression

    // Decompressed data
    std::vector<char> m_decompressedData;

    // We store the original size for clarity
    int m_originalSize;

    // The output file
    FILE* m_outFile;

    // Size for large enough buffer for worst-case LZ4 compression
    int m_maxDstSize;
};

#endif // LZ4_PIPE_HPP