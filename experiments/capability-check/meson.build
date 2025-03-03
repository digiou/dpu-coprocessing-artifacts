project('DOCA_CAPABILITY_CHECK', 'C', 'CPP',
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
    dependency('doca-common'),
    dependency('doca-compress'),
    dependency('doca-argp')
]

# Shared include directories
common_include_dirs = [
    include_directories('/opt/mellanox/doca/samples/doca_compress'),
    include_directories('/opt/mellanox/doca/samples'),
    include_directories('/opt/mellanox/doca'),
    include_directories('/opt/mellanox/doca/applications/common/'),
    include_directories('/opt/mellanox/doca/include/')
]

# ---------------- First Binary: capability_check ----------------

sample_name = 'capability_check'
capability_check_srcs = [
    sample_name + '_main.c',
    '/opt/mellanox/doca/samples/doca_compress/compress_common.c',
    '/opt/mellanox/doca/samples/common.c',
]

executable('doca_' + sample_name, capability_check_srcs,
    c_args : '-Wno-missing-braces',
    dependencies : common_dependencies,
    include_directories: common_include_dirs,
    install: false)