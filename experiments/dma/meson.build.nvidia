project('DOCA_DMA_BENCH', 'C', 'CPP',
    # Get version number from file.
    version: run_command(find_program('cat'),
        files('/opt/mellanox/doca/applications/VERSION'), check: true).stdout().strip(),
    license: 'Proprietary',
    default_options: ['buildtype=debug'],
    meson_version: '>= 0.61.2'
)

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: ['c', 'cpp'])

# ---------------- Shared Dependencies and Includes ----------------

# Shared dependencies
common_dependencies = [
    dependency('threads'),
    dependency('doca-common'),
    dependency('doca-argp'),
    dependency('doca-dma'),
    dependency('doca-comch')
]

# Shared include directories
common_include_dirs = [
    include_directories('/opt/mellanox/doca'),
    include_directories('/opt/mellanox/doca/applications/common/'),
    include_directories('/opt/mellanox/doca/include/'),
]

# ---------------- First Binary: dma_bench ----------------

sample_name = 'dma_bench'
dma_bench_srcs = [
    'dma_copy_core.c',
    'dma_bench.c',
    '/opt/mellanox/doca/applications/common/comch_utils.c',
    '/opt/mellanox/doca/applications/common/pack.c',
    '/opt/mellanox/doca/applications/common/utils.c',
    '/opt/mellanox/doca/samples/common.c',
]

base_c_args = [
        '-Wno-missing-braces',
        '-Wno-missing-field-initializers',
]

executable('doca_' + sample_name, 
    dma_bench_srcs,
    c_args : base_c_args,
    dependencies : common_dependencies,
    include_directories: common_include_dirs,
    install: false)
