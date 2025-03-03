project('DOCA_REGEX', 'C',
	version: '1.0.0',
	license: 'Proprietary',
	default_options: ['buildtype=debug'],
	meson_version: '>= 0.61.2'
)

DOCA_PREFIX = 'doca_'

install_apps = false

languages = ['c']

base_c_args = [
	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
]

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: languages)

# Resolve irrelevant compiler warnings
add_project_arguments('-Wno-format-zero-length', language: languages)

doca_applications_path = '/opt/mellanox/doca/applications/'
common_path = 'common/src'
common_dir_path = doca_applications_path + common_path
application_path = 'file_scan/src'
samples_dir_path = doca_applications_path + '../samples'

base_app_inc_dirs = [
	include_directories(common_dir_path),
	include_directories(doca_applications_path + '..'),
	include_directories(samples_dir_path),
	include_directories(doca_applications_path + application_path),
]

source_path = 'src'

app_inc_dirs = base_app_inc_dirs

app_dependencies = []
app_dependencies += dependency('doca')

app_srcs = []
app_srcs += [
	'regex_scan.c',
	common_dir_path + '/pack.c',
	common_dir_path + '/utils.c',
	samples_dir_path + '/common.c',
	doca_applications_path + application_path + '/file_scan_core.c',
]

vanilla_app_srcs = [
	'regex_scan' + '.c',
]

executable(DOCA_PREFIX + 'regex_scan',
	app_srcs + vanilla_app_srcs,
	c_args : base_c_args,
	dependencies : app_dependencies,
	include_directories : app_inc_dirs,
	install: install_apps)
