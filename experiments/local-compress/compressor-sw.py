import zlib, sys

def main():
    """
    Produce valid DEFLATE stream without header zlib header for DOCA Decompress.
    Closest compression level to HW DOCA was 3.
    """
    with open(sys.argv[1], 'rb') as input_bytes:
        file_data = input_bytes.read()
        # calculate_checksum(file_data)
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
    main()
