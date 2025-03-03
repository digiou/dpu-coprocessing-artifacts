#include "zpipe.hpp"

Zpipe::Zpipe() : m_inFile(nullptr), m_outFile(nullptr), m_deflateLevel(Z_DEFAULT_COMPRESSION) {
    // Zero out the z_stream
    std::memset(&stream, 0, sizeof(stream));
}

int Zpipe::readFileInChunks() {
    while (true) {
        std::vector<unsigned char> buffer(CHUNK);

        // Read up to CHUNK bytes
        size_t bytesRead = std::fread(buffer.data(), 1, CHUNK, m_inFile);
        if (std::ferror(m_inFile)) {
            return Z_ERRNO;
        }
        if (bytesRead == 0) {
            // no more data or hit EOF
            break;
        }

        // Shrink the vector to the actual bytes read (important if < CHUNK)
        buffer.resize(bytesRead);
        m_inputChunks.push_back(std::move(buffer));

        // If we reached EOF, break
        if (std::feof(m_inFile)) {
            break;
        }
    }
    return Z_OK;
}

int Zpipe::readFileFully() {
    // read entire file into m_fullInput
    m_fullInput.clear();

    unsigned char temp[CHUNK];
    while (true) {
        size_t bytesRead = std::fread(temp, 1, CHUNK, m_inFile);
        if (std::ferror(m_inFile)) {
            return Z_ERRNO;
        }
        if (bytesRead == 0) {
            break; // EOF
        }
        // Append to m_fullInput
        size_t oldSize = m_fullInput.size();
        m_fullInput.resize(oldSize + bytesRead);
        std::memcpy(m_fullInput.data() + oldSize, temp, bytesRead);

        if (std::feof(m_inFile)) {
            break;
        }
    }
    return Z_OK;
}

int Zpipe::m_init(const std::string &inFilename, const std::string &outFilename, bool inflate, bool singleBufferExecution) {
    // 1) Open input file
    m_inFile = std::fopen(inFilename.c_str(), "rb");
    if (!m_inFile) {
        std::cerr << "Failed to open input file: " << inFilename << "\n";
        return Z_ERRNO;
    }

    if (!singleBufferExecution) {
        // 2) Read the entire file in CHUNK_SIZE increments into memory
        int readStatus = readFileInChunks();
        if (readStatus != Z_OK) {
            std::cerr << "Error reading file in chunks.\n";
            std::fclose(m_inFile);
            m_inFile = nullptr;
            return readStatus;
        }
    } else {
        // 2.b) Read the entire file in a single buffer, imitating DOCA
        auto readStatus = readFileFully();
        if (readStatus != Z_OK) {
            std::cerr << "Error reading file in full.\n";
            std::fclose(m_inFile);
            m_inFile = nullptr;
            return readStatus;
        }
    }

    // We no longer need the input file on disk; we have all data in memory
    std::fclose(m_inFile);
    m_inFile = nullptr;

    // 3) Open output file
    m_outFile = std::fopen(outFilename.c_str(), "wb");
    if (!m_outFile) {
        std::cerr << "Failed to open output file: " << outFilename << "\n";
        // Clear out input chunks and single buffer if needed
        m_inputChunks.clear();
        m_fullInput.clear();
        return Z_ERRNO;
    }

    // 4) Init zstream struct
    this->stream.zalloc = Z_NULL;
    this->stream.zfree = Z_NULL;
    this->stream.opaque = Z_NULL;
    int ret = Z_ERRNO;
    if (inflate) {
        this->stream.avail_in = 0;
        this->stream.next_in = Z_NULL;
        ret = inflateInit(&this->stream);
    } else {
        ret = deflateInit(&this->stream, m_deflateLevel);
    }

    if (ret != Z_OK) {
        // Clear chunks and single buffer
        m_inputChunks.clear();
        m_fullInput.clear();
        // close files
        std::fclose(m_outFile);
        m_outFile = nullptr;
        return ret;
    }

    // Mark execution style for later stages
    this->singleBufferExecution = singleBufferExecution;

    return Z_OK;
}

int Zpipe::deflate_init(const std::string &inFilename, const std::string &outFilename, bool singleBufferExecution) {
    int ret = this->m_init(inFilename, outFilename, false, singleBufferExecution);
    if (ret != Z_OK) {
        std::cerr << "Failed to init DEFLATE"<< std::endl;
    }
    return ret;
}

int Zpipe::inflate_init(const std::string &inFilename, const std::string &outFilename, bool singleBufferExecution) {
    int ret = this->m_init(inFilename, outFilename, true, singleBufferExecution);
    if (ret != Z_OK) {
        std::cerr << "Failed to init INFLATE"<< std::endl;
    }
    return ret;
}

int Zpipe::deflate_execute() {
    if (m_inputChunks.empty() || !m_outFile) {
        std::cerr << "No input data or no output file open. Did init() fail?\n";
        return Z_ERRNO;
    }

    m_compressedChunks.clear();

    int ret = Z_OK;
    // We'll feed each chunk to deflate in turn
    for (size_t i = 0; i < m_inputChunks.size(); ++i) {
        auto &chunk = m_inputChunks[i];
        this->stream.avail_in = static_cast<uInt>(chunk.size());
        this->stream.next_in  = chunk.data();

        // If it's the last chunk, we use Z_FINISH eventually.
        int flush = (i == m_inputChunks.size() - 1) ? Z_FINISH : Z_NO_FLUSH;

        do {
            // Temporary buffer for compressed data
            unsigned char outBuf[CHUNK];
            this->stream.avail_out = CHUNK;
            this->stream.next_out  = outBuf;

            ret = deflate(&this->stream, flush);
            assert(ret != Z_STREAM_ERROR);

            // Calculate how many bytes were compressed
            size_t have = CHUNK - this->stream.avail_out;
            if (have > 0) {
                // Store this compressed piece in m_compressedChunks
                m_compressedChunks.emplace_back(have);
                auto &compressedVec = m_compressedChunks.back();
                std::memcpy(compressedVec.data(), outBuf, have);
            }
        } while (this->stream.avail_out == 0);
        // We should have consumed all input for this chunk
        assert(this->stream.avail_in == 0);
        if (ret == Z_STREAM_END) {
            break; // If we hit end earlier than we expected, break
        }
    }
    // Ideally, the final call with Z_FINISH should yield ret == Z_STREAM_END
    assert(ret == Z_STREAM_END);
    return ret;
}

int Zpipe::deflate_execute_single_buffer() {
    // 1) Make sure we have data to compress
    if (m_fullInput.empty()) {
        std::cerr << "No input data in m_fullInput.\n";
        return Z_ERRNO;
    }

    // 2) Clear any old compressed data
    m_fullOutput.clear();

    // 3) Tell zlib we have the entire file in memory
    this->stream.avail_in = static_cast<uInt>(m_fullInput.size());
    this->stream.next_in  = reinterpret_cast<Bytef*>(m_fullInput.data());

    // 4) We'll call deflate with Z_FINISH in a loop until it returns Z_STREAM_END
    int ret = Z_OK;

    // A small buffer used to grab compressed output from zlib
    unsigned char outBuf[CHUNK];
    do {
        this->stream.avail_out = CHUNK;
        this->stream.next_out  = outBuf;

        // Because we've handed zlib all of our data at once,
        // we can tell it to FINISH immediately.
        ret = deflate(&this->stream, Z_FINISH);

        // This assertion ensures we didn't somehow break zlib’s state
        assert(ret != Z_STREAM_ERROR);

        // How many bytes did we get back?
        size_t have = CHUNK - this->stream.avail_out;
        if (have > 0) {
            // Append these 'have' bytes to our in-memory compressed data
            size_t oldSize = m_fullOutput.size();
            m_fullOutput.resize(oldSize + have);
            std::memcpy(m_fullOutput.data() + oldSize, outBuf, have);
        }

        // We'll keep looping as long as zlib produces data,
        // or until it returns Z_STREAM_END (done).
    } while (this->stream.avail_out == 0);

    // At this point, if everything worked, ret should be Z_STREAM_END.
    assert(ret == Z_STREAM_END);

    // 5) Done — we haven’t yet called deflateEnd() because we might do that in cleanup()
    return Z_OK;
}

int Zpipe::inflate_execute_single_buffer() {
    // 1) If we have no compressed data, nothing to do
    if (m_fullInput.empty()) {
        std::cerr << "No data in memory!\n";
        return Z_ERRNO;
    }

    // 2) Clear any old decompressed data
    m_fullOutput.clear();

    // 3) Provide the entire compressed buffer to zlib
    this->stream.avail_in = static_cast<uInt>(m_fullInput.size());
    this->stream.next_in  = (Bytef*)m_fullInput.data();

    int ret = Z_OK;

    // We'll call inflate repeatedly until it returns Z_STREAM_END or an error
    do {
        // We allocate a temporary buffer to hold decompressed output
        unsigned char outBuf[CHUNK];
        this->stream.avail_out = CHUNK;
        this->stream.next_out = outBuf;

        // In a single-buffer scenario, we typically do Z_NO_FLUSH 
        // until we've exhausted the input data
        ret = inflate(&this->stream, Z_NO_FLUSH);
        assert(ret != Z_STREAM_ERROR);

        if (ret == Z_NEED_DICT) {
            // If a dictionary is needed, you either supply it or treat as error
            ret = Z_DATA_ERROR;
        }
        if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&this->stream);
            return ret;
        }

        size_t have = CHUNK - this->stream.avail_out;
        if (have > 0) {
            // Append these decompressed bytes to m_fullOutput
            size_t oldSize = m_fullOutput.size();
            m_fullOutput.resize(oldSize + have);
            std::memcpy(m_fullOutput.data() + oldSize, outBuf, have);
        }
        // We'll keep calling inflate until it returns Z_STREAM_END (or an error)
        // But because we gave it all the input at once, 
        // we might see ret = Z_OK multiple times until the stream is exhausted
    } while (ret != Z_STREAM_END);
    
    // If we reach here, ret should be Z_STREAM_END
    return (ret == Z_STREAM_END) ? Z_OK : Z_DATA_ERROR;;
}

void Zpipe::m_cleanup(bool inflate) {
    // 1) End deflate if it's been initialized
    if (inflate) {
        inflateEnd(&this->stream);
    } else {
        deflateEnd(&this->stream);
    }
    
    // 2) Close output file if open, write results if any
    if (m_outFile) {
        if (this->singleBufferExecution) {
            size_t written = std::fwrite(m_fullOutput.data(), 1, 
                                         m_fullOutput.size(), m_outFile);
            if (written != m_fullOutput.size() || std::ferror(m_outFile)) {
                std::cerr << "Error writing FULL data." << std::endl;
            }
        } else {
            for (auto &compressedChunk : m_compressedChunks) {
                size_t written = std::fwrite(compressedChunk.data(), 1, compressedChunk.size(), m_outFile);
                if (written != compressedChunk.size() || std::ferror(m_outFile)) {
                    std::cerr << "Error writing data." << std::endl;
                    break;
                }
            }
        }
        std::fclose(m_outFile);
        m_outFile = nullptr;
    }

    // 3) Clear in-memory data
    m_inputChunks.clear();
    m_compressedChunks.clear();
    m_fullInput.clear();
    m_fullOutput.clear();
}

void Zpipe::deflate_cleanup() {
    this->m_cleanup(false);
}

void Zpipe::inflate_cleanup() {
    this->m_cleanup(true);
}

int Zpipe::def(FILE *source, FILE *dest, int level){
    std::cout << "Starting zstream def..." << std::endl;
    int ret, flush;
    unsigned have;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    std::cout << "Start zstream processing..." << std::endl;

    this->stream.zalloc = Z_NULL;
    this->stream.zfree = Z_NULL;
    this->stream.opaque = Z_NULL;
    ret = deflateInit(&this->stream, level);
    if (ret != Z_OK)
        return ret;

    do {
        this->stream.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)deflateEnd(&this->stream);
            return Z_ERRNO;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        this->stream.next_in = in;

        do {
            this->stream.avail_out = CHUNK;
            this->stream.next_out = out;
            ret = deflate(&this->stream, flush);    /* anyone error value */
            assert(ret != Z_STREAM_ERROR);
            have = CHUNK - this->stream.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&this->stream);
                return Z_ERRNO;
            }
        } while (this->stream.avail_out == 0);
        assert(this->stream.avail_in == 0);

    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);

    /* limpar e retornar */
    (void)deflateEnd(&this->stream);
    return Z_OK;
}

int Zpipe::inf(FILE *source, FILE *dest){
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;

    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);

    } while (ret != Z_STREAM_END);

    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

void Zpipe::zerr(int ret) {
    std::cerr << "zpipe: ";
    switch (ret) {
    case Z_ERRNO:
        if (ferror(stdin)){
            std::cerr << "Error to read stdin . " << '\n';
        }else if (ferror(stdout)){
            std::cerr << "Error to writing stdout." << '\n';
        }
        break;
    case Z_STREAM_ERROR:
        std::cerr << "Invalid compression level . " << '\n';
        break;
    case Z_DATA_ERROR:
        std::cerr << "Empty data, invalid or incomplete. " << '\n';
        break;
    case Z_MEM_ERROR:
        std::cerr << "No memory. " << '\n';
        break;
    case Z_VERSION_ERROR:
        std::cerr << "zlib version is incompatible. " << '\n';
    }
}
