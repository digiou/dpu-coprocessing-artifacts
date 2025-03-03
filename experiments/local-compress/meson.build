project('DOCA_SAMPLE', 'C', 'CPP',
    # Get version number from file.
    version: run_command(find_program('cat'),
        files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
    license: 'Proprietary',
    default_options: ['buildtype=release', 'cpp_std=c++20'],
    meson_version: '>= 0.61.2'
)

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

# ---------------- Shared Dependencies and Includes ----------------

# Shared dependencies
common_dependencies = [
    dependency('doca-common'),
    dependency('doca-compress'),
    dependency('doca-argp')
]

# Shared include directories
common_include_dirs = [
    include_directories('/opt/mellanox/doca/samples'),
    include_directories('/opt/mellanox/doca'),
    include_directories('/opt/mellanox/doca/applications/common/')
]

# ---------------- First Binary: compress_deflate ----------------

compress_sample_name = 'compress_deflate'
compress_sample_srcs = [
    compress_sample_name + '_sample.c',
    compress_sample_name + '_main.c',
    'compress_common.c',
    '/opt/mellanox/doca/samples/common.c',
    '/opt/mellanox/doca/applications/common/utils.c',
]

executable('doca_' + compress_sample_name, compress_sample_srcs,
    c_args : '-Wno-missing-braces',
    dependencies : common_dependencies,
    include_directories: common_include_dirs,
    install: false)

# ---------------- Second Binary: decompress_deflate ----------------

decompress_sample_name = 'decompress_deflate'
decompress_sample_srcs = [
    decompress_sample_name + '_sample.c',
    decompress_sample_name + '_main.c',
    'compress_common.c',
    '/opt/mellanox/doca/samples/common.c',
    '/opt/mellanox/doca/applications/common/utils.c',
]

executable('doca_' + decompress_sample_name, decompress_sample_srcs,
    c_args : '-Wno-missing-braces',
    dependencies : common_dependencies,
    include_directories: common_include_dirs,
    install: false)


# ---------------- Third Binary: decompress_lz4_stream ----------------

decompress_lz4_stream_sample_name = 'decompress_lz4_stream'
decompress_lz4_stream_sample_srcs = [
    decompress_lz4_stream_sample_name + '_sample.c',
    decompress_lz4_stream_sample_name + '_main.c',
    'compress_common.c',
    '/opt/mellanox/doca/samples/common.c',
    '/opt/mellanox/doca/applications/common/utils.c',
]

executable('doca_' + decompress_lz4_stream_sample_name, decompress_lz4_stream_sample_srcs,
	c_args : '-Wno-missing-braces',
	dependencies : common_dependencies,
	include_directories: common_include_dirs,
	install: false)

# ---------------- Fourth Binary: compress_deflate_faster ----------------

compress_faster_name = 'compress_deflate_faster'
compress_faster_srcs = [
    compress_faster_name + '.c',
    'compress_common.c',
    '/opt/mellanox/doca/samples/common.c',
    '/opt/mellanox/doca/applications/common/utils.c',
]

executable('doca_' + compress_faster_name, compress_faster_srcs,
    c_args : '-Wno-missing-braces',
    dependencies : common_dependencies,
    include_directories: common_include_dirs,
    install: false)

# ---------------- Fifth Binary: decompress_deflate_faster ----------------

decompress_faster_name = 'decompress_deflate_faster'
decompress_faster_srcs = [
    decompress_faster_name + '.c',
    'compress_common.c',
    '/opt/mellanox/doca/samples/common.c',
    '/opt/mellanox/doca/applications/common/utils.c',
]

executable('doca_' + decompress_faster_name, decompress_faster_srcs,
    c_args : '-Wno-missing-braces',
    dependencies : common_dependencies,
    include_directories: common_include_dirs,
    install: false)

# ---------------- Sixth Binary: decompress_lz4_block_faster ----------------

decompress_lz4_block_faster_name = 'decompress_lz4_block_faster'
decompress_lz4_block_faster_srcs = [
    decompress_lz4_block_faster_name + '.c',
    'compress_common.c',
    '/opt/mellanox/doca/samples/common.c',
    '/opt/mellanox/doca/applications/common/utils.c',
]

executable('doca_' + decompress_lz4_block_faster_name, decompress_lz4_block_faster_srcs,
    c_args : '-Wno-missing-braces',
    dependencies : common_dependencies,
    include_directories: common_include_dirs,
    install: false)