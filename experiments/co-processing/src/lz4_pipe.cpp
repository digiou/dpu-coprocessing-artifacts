#include <iostream>
#include <cstdio>
#include <cstring>   // for memcpy
#include "lz4.h"     // LZ4 one-shot API

#include "lz4_pipe.hpp"

static const size_t READ_CHUNK = 16384;

LZ4Pipe::LZ4Pipe() : m_compressedSize(0), m_originalSize(0), m_outFile(nullptr), m_maxDstSize(0) {}

LZ4Pipe::~LZ4Pipe() {
    // No special cleanup needed
}

int LZ4Pipe::readInputFile(const std::string &filename) {
    m_originalData.clear();

    FILE* fp = std::fopen(filename.c_str(), "rb");
    if (!fp) {
        std::cerr << "Could not open " << filename << "\n";
        return -1;
    }

    while (true) {
        char temp[READ_CHUNK];
        size_t bytesRead = std::fread(temp, 1, READ_CHUNK, fp);
        if (std::ferror(fp)) {
            std::fclose(fp);
            return -1;
        }
        if (bytesRead == 0) {
            break; // EOF
        }
        // Append
        size_t oldSize = m_originalData.size();
        m_originalData.resize(oldSize + bytesRead);
        std::memcpy(m_originalData.data() + oldSize, temp, bytesRead);

        if (std::feof(fp)) {
            break;
        }
    }

    std::fclose(fp);

    m_originalSize = static_cast<int>(m_originalData.size());
    return 0;
}

int LZ4Pipe::compressInMemory() {
    // If there's no original data, nothing to do
    if (m_originalSize <= 0) {
        std::cerr << "No data to compress.\n";
        return -1;
    }

    // Allocate an output buffer large enough for worst-case LZ4 compression
    m_maxDstSize = LZ4_compressBound(m_originalSize);
    m_compressedData.resize(m_maxDstSize);

    // LZ4_compress_default returns number of bytes in compressed data
    m_compressedSize = LZ4_compress_default(
        m_originalData.data(),        // source
        m_compressedData.data(),      // dest
        m_originalSize,               // source size
        m_maxDstSize                    // max capacity of dest
    );

    if (m_compressedSize <= 0) {
        std::cerr << "LZ4 compression failed.\n";
        return -1;
    }

    // Shrink to actual compressed size
    m_compressedData.resize(m_compressedSize);

    return 0;
}

int LZ4Pipe::decompress_init(const std::string &inputFile, const std::string &outputFile) {
    // 1) Read the entire uncompressed file
    int ret = readInputFile(inputFile);
    if (ret != 0) {
        std::cerr << "Failed to read input file.\n";
        return ret;
    }

    // 2) Compress it in memory so we have something to decompress
    ret = compressInMemory();
    if (ret != 0) {
        std::cerr << "Failed to compress data in memory.\n";
        return ret;
    }

    // Now m_compressedData holds a valid LZ4 blob
    // We do NOT yet decompress it. We'll do that in execute().

    // 3) Open output file
    m_outFile = std::fopen(outputFile.c_str(), "wb");
    if (!m_outFile) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        // Clear out input chunks and single buffer if needed
        m_originalData.clear();
        return -1;
    }

    // 4) Init decompress buffers from m_compressedData -> m_decompressedData
    m_decompressedData.clear();

    // We DO know the original size (m_originalSize),
    // so we can allocate exactly that.
    m_decompressedData.resize(m_originalSize);

    return 0;
}

int LZ4Pipe::decompress_execute() {
    // LZ4_decompress_safe returns the number of decompressed bytes or an error
    int decompressedBytes = LZ4_decompress_safe(
        m_compressedData.data(),           // src
        m_decompressedData.data(),         // dst
        m_compressedSize,                  // compressed size
        m_originalSize                     // max output size
    );

    if (decompressedBytes < 0) {
        std::cerr << "LZ4 decompression failed.\n";
        return -1;
    }

    // Typically, decompressedBytes should match m_originalSize if everything’s correct
    if (decompressedBytes != m_originalSize) {
        std::cerr << "Warning: Decompressed size mismatch. Got "
                  << decompressedBytes << " vs expected " << m_originalSize << "\n";
    }

    return 0;
}

void LZ4Pipe::decompress_cleanup() {
    // Write the decompressed data to disk, if you'd like to validate correctness
    if (m_outFile) {
        if (!m_decompressedData.empty()) {
            size_t written = std::fwrite(m_decompressedData.data(), 1,
                                         m_decompressedData.size(), m_outFile);
            if (written != m_decompressedData.size() || std::ferror(m_outFile)) {
                std::cerr << "Error writing decompressed data.\n";
            }
        }
        std::fclose(m_outFile);
        m_outFile = nullptr;
    }

    // Clear everything if desired
    m_originalData.clear();
    m_compressedData.clear();
    m_decompressedData.clear();
    m_compressedSize = 0;
    m_originalSize = 0;
    m_maxDstSize = 0;
}

int LZ4Pipe::compress_init(const std::string &inputFile, const std::string &outputFile) {
    // 1) Read the entire uncompressed file
    int ret = readInputFile(inputFile);
    if (ret != 0) {
        std::cerr << "Failed to read input file.\n";
        return ret;
    }

    // If there's no original data, nothing to do
    if (m_originalSize <= 0) {
        std::cerr << "No data to compress.\n";
        return -1;
    }

    // 2) Open output file
    m_outFile = std::fopen(outputFile.c_str(), "wb");
    if (!m_outFile) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        // Clear out input chunks and single buffer if needed
        m_originalData.clear();
        m_originalSize = 0;
        return -1;
    }

    // 3) Allocate an output buffer large enough for worst-case LZ4 compression
    m_maxDstSize = LZ4_compressBound(m_originalSize);
    m_compressedData.resize(m_maxDstSize);

    return 0;
}

int LZ4Pipe::compress_execute() {
    // LZ4_compress_default returns number of bytes in compressed data
    m_compressedSize = LZ4_compress_default(
        m_originalData.data(),        // source
        m_compressedData.data(),      // dest
        m_originalSize,               // source size
        m_maxDstSize                  // max capacity of dest
    );

    if (m_compressedSize <= 0) {
        std::cerr << "LZ4 compression failed.\n";
        return -1;
    }

    return 0;
}

void LZ4Pipe::compress_cleanup() {
    // Shrink to actual compressed size
    m_compressedData.resize(m_compressedSize);

    // Write the decompressed data to disk, if you'd like to validate correctness
    if (m_outFile) {
        if (!m_compressedData.empty()) {
            size_t written = std::fwrite(m_compressedData.data(), 1,
                                         m_compressedData.size(), m_outFile);
            if (written != m_compressedData.size() || std::ferror(m_outFile)) {
                std::cerr << "Error writing decompressed data.\n";
            }
        }
        std::fclose(m_outFile);
        m_outFile = nullptr;
    }

    // Clear everything if desired
    m_originalData.clear();
    m_compressedData.clear();
    m_decompressedData.clear();
    m_compressedSize = 0;
    m_originalSize = 0;
    m_maxDstSize = 0;
}
