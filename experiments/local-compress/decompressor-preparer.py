import argparse
import sys
import zlib

import lz4.block


def main():
    """
    Produce valid DEFLATE stream without header zlib header for DOCA Decompress.
    Closest compression level to HW DOCA was 3.
    """
    with open(sys.argv[1], 'rb') as input_bytes:
        file_data = input_bytes.read()
        zlib_compress_data = zlib.compress(file_data, int(sys.argv[2]))
        # compressed_checksum_bytes = zlib_compress_data[-4:]  # Adler32 from zlib only
        # compressed_checksum = int.from_bytes(compressed_checksum_bytes, byteorder='big')
        # print("Extracted checksum from compressed data:", compressed_checksum)
        # bytes_before_chksm = int.from_bytes(zlib_compress_data[-8:-4], byteorder='big')
        # print("4 bytes before extracted checksum: ", bytes_before_chksm)
    with open('python-comp.txt', 'wb') as output_file:
        output_file.write(zlib_compress_data[2:])
    # Below is for testing in a debugger only
    # with open('input-comp.txt', 'rb') as doca_file:
    #     doca_data = doca_file.read()
    #     doca_checksum_bytes = doca_data[-4:]
    #     doca_checksum = int.from_bytes(doca_checksum_bytes, byteorder='big')
    #     print("DOCA file last 4 bytes:", doca_checksum)
    #     doca_decompressed = zlib.decompress(doca_data, -zlib.MAX_WBITS)
    # with open('python-decomp.txt', 'wb') as decompressed_from_doca:
    #     decompressed_from_doca.write(doca_decompressed)


def calculate_checksum(file_data):
    # Calculate CRC-32 checksum
    crc = zlib.crc32(file_data)
    # print("CRC32 from compressed data:", crc)

    # Calculate Adler-32 checksum
    adler = zlib.adler32(file_data)
    # print("Adler32 from compressed data:", adler)

    # Combine both checksums into a single 64-bit integer, as DOCA computes it
    # Adler-32 is placed in the upper 32 bits and CRC-32 in the lower 32 bits
    result_checksum = (adler << 32) | crc
    # print("Combined DOCA checksum:", result_checksum)
    return result_checksum


if __name__ == '__main__':
    parser = argparse.ArgumentParser(prog='compressor-dpu', allow_abbrev=False)
    parser.add_argument('--file', type=str, default="input-file", help="file location")
    parser.add_argument('--compress_level', default=2, type=int, help="comp lvl", required=False)
    parser.add_argument('--lz4_compress_level', default=2, type=int, help="lz4 comp lvl", required=False)
    parser.add_argument('--chunk_size', default=2 * 1024 * 1024, type=int, help="max chunk size in bytes",
                        required=False)
    args = parser.parse_args()

    # We'll read the file chunk-by-chunk, each chunk up to args.chunk_size bytes.
    with open(args.file, 'rb') as f:
        chunk_index = 0
        total_original = 0

        while True:
            chunk = f.read(args.chunk_size)
            if not chunk:
                break

            chunk_index += 1
            original_len = len(chunk)
            total_original += original_len

            # Compress with a fresh raw DEFLATE compressor (independent block)
            compressor = zlib.compressobj(
                level=args.compress_level,
                method=zlib.DEFLATED,
                wbits=-15  # negative wbits => raw DEFLATE
            )
            compressed_data = compressor.compress(chunk) + compressor.flush()

            compressed_data_lz4 = lz4.block.compress(chunk, store_size=False)
            assert lz4.block.decompress(compressed_data_lz4, uncompressed_size=len(chunk)) == chunk, "Round-trip mismatch!"

            if chunk_index == 1:
                first_compressed_chunk = compressed_data  # eff it
                first_compressed_len = len(compressed_data)
                first_compressed_chunk_lz4 = compressed_data_lz4
                first_compressed_len_lz4 = len(compressed_data_lz4)

        with open(f'compressed-{chunk_index}-{first_compressed_len}.deflate', 'wb') as write_bytes:
            for idx in range(chunk_index):
                write_bytes.write(first_compressed_chunk)

        with open(f'compressed-{chunk_index}-{first_compressed_len_lz4}.lz4', 'wb') as write_bytes_lz4:
            for idx in range(chunk_index):
                write_bytes_lz4.write(first_compressed_chunk_lz4)

        print(total_original, chunk_index, first_compressed_len, first_compressed_len_lz4)
