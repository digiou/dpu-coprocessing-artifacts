# This is an automatically generated file, and it shows.
ninja-default:
.PHONY: ninja-default
vpath
NULL :=
SPACE := ${NULL} #
MAKEFLAGS += -rR
define NEWLINE

endef
.var.in_newline = $(subst $(SPACE),$(NEWLINE),${.var.in})
ninja-command = $(if $V,,$(if ${.var.description},@printf '%s\n' '$(subst ','\'',${.var.description})' && ))${.var.command}
ninja-command-restat = $(if $V,,$(if ${.var.description},@printf '%s\n' '$(subst ','\'',${.var.description})' && ))${.var.command} && if test -e $(firstword ${.var.out}); then printf '%s\n' ${.var.out} > $@; fi
.var.ninja_required_version := 1.8.2
PHONY:
doca_compression_local.p/compression_local.c.o: compression_local.c | ; ${ninja-command}
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o: /opt/mellanox/doca/applications/common/src/pack.c | ; ${ninja-command}
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o: /opt/mellanox/doca/applications/common/src/utils.c | ; ${ninja-command}
doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o: /opt/mellanox/doca/samples/common.c | ; ${ninja-command}
doca_compression_local: doca_compression_local.p/compression_local.c.o doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_apsh.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_argp.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_comm_channel.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_common.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_compress.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dma.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpa.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpdk_bridge.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpi.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_erasure_coding.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_eth.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_flow.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_flow_ct.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_ipsec.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_pcc.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_rdma.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_regex.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_sha.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_telemetry.so /usr/lib/aarch64-linux-gnu/libbsd.so /usr/lib/aarch64-linux-gnu/libjson-c.so /usr/lib/aarch64-linux-gnu/libz.so | ; ${ninja-command}

all: doca_compression_local
clean:
	rm -rf doca_compression_local.p doca_compression_local input-*.txt container-applications/* container-include/* container-samples/*

ninja-default: all

ninja-outputdirs :=
ninja-targets-c_COMPILER := doca_compression_local.p/compression_local.c.o doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o
$(ninja-targets-c_COMPILER): .var.command = cc ${.var.ARGS} -MD -MQ ${.var.out} -MF ${.var.DEPFILE} -o ${.var.out} -c ${.var.in}
$(ninja-targets-c_COMPILER): .var.deps = gcc
$(ninja-targets-c_COMPILER): .var.depfile = ${.var.DEPFILE_UNQUOTED}
$(ninja-targets-c_COMPILER): .var.description = Compiling C object ${.var.out}
ninja-outputdirs += $(sort $(dir ${ninja-targets-c_COMPILER}))

ninja-targets-c_LINKER := doca_compression_local
$(ninja-targets-c_LINKER): .var.command = cc ${.var.ARGS} -o ${.var.out} ${.var.in} ${.var.LINK_ARGS}
$(ninja-targets-c_LINKER): .var.description = Linking target ${.var.out}
ninja-outputdirs += $(sort $(dir ${ninja-targets-c_LINKER}))

dummy := $(shell mkdir -p . $(sort $(ninja-outputdirs)))
ninja-depfiles :=doca_compression_local.p/compression_local.c.o.d doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o.d doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o.d doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o.d
ninja-rspfiles :=
-include ${ninja-depfiles}

doca_compression_local.p/compression_local.c.o: .var.out := doca_compression_local.p/compression_local.c.o
doca_compression_local.p/compression_local.c.o: .var.in := compression_local.c
doca_compression_local.p/compression_local.c.o: .var.DEPFILE := doca_compression_local.p/compression_local.c.o.d
doca_compression_local.p/compression_local.c.o: .var.DEPFILE_UNQUOTED := doca_compression_local.p/compression_local.c.o.d
doca_compression_local.p/compression_local.c.o: .var.ARGS := -Idoca_compression_local.p -I. -I.. -I/opt/mellanox/doca/applications/common/src -I/opt/mellanox/doca -I/usr/include/json-c -I/opt/mellanox/doca/include -I/opt/mellanox/flexio/include -I/opt/mellanox/dpdk/include/dpdk -I/opt/mellanox/dpdk/include/dpdk/../aarch64-linux-gnu/dpdk -I/usr/include/libnl3 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -fdiagnostics-color=always -D_FILE_OFFSET_BITS=64 -Wall -Winvalid-pch -g '-D DOCA_ALLOW_EXPERIMENTAL_API' -Wno-format-zero-length '-D DOCA_USE_LIBBSD' '-D RTE_USE_LIBBSD' -include rte_config.h -mcpu=cortex-a72 -DALLOW_EXPERIMENTAL_API -pthread -Wno-missing-braces -Wno-missing-field-initializers

doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o: .var.out := doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o: .var.in := /opt/mellanox/doca/applications/common/src/pack.c
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o: .var.DEPFILE := doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o.d
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o: .var.DEPFILE_UNQUOTED := doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o.d
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o: .var.ARGS := -Idoca_compression_local.p -I. -I.. -I/opt/mellanox/doca/applications/common/src -I/opt/mellanox/doca -I/usr/include/json-c -I/opt/mellanox/doca/include -I/opt/mellanox/flexio/include -I/opt/mellanox/dpdk/include/dpdk -I/opt/mellanox/dpdk/include/dpdk/../aarch64-linux-gnu/dpdk -I/usr/include/libnl3 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -fdiagnostics-color=always -D_FILE_OFFSET_BITS=64 -Wall -Winvalid-pch -g '-D DOCA_ALLOW_EXPERIMENTAL_API' -Wno-format-zero-length '-D DOCA_USE_LIBBSD' '-D RTE_USE_LIBBSD' -include rte_config.h -mcpu=cortex-a72 -DALLOW_EXPERIMENTAL_API -pthread -Wno-missing-braces -Wno-missing-field-initializers

doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o: .var.out := doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o: .var.in := /opt/mellanox/doca/applications/common/src/utils.c
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o: .var.DEPFILE := doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o.d
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o: .var.DEPFILE_UNQUOTED := doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o.d
doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o: .var.ARGS := -Idoca_compression_local.p -I. -I.. -I/opt/mellanox/doca/applications/common/src -I/opt/mellanox/doca -I/usr/include/json-c -I/opt/mellanox/doca/include -I/opt/mellanox/flexio/include -I/opt/mellanox/dpdk/include/dpdk -I/opt/mellanox/dpdk/include/dpdk/../aarch64-linux-gnu/dpdk -I/usr/include/libnl3 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -fdiagnostics-color=always -D_FILE_OFFSET_BITS=64 -Wall -Winvalid-pch -g '-D DOCA_ALLOW_EXPERIMENTAL_API' -Wno-format-zero-length '-D DOCA_USE_LIBBSD' '-D RTE_USE_LIBBSD' -include rte_config.h -mcpu=cortex-a72 -DALLOW_EXPERIMENTAL_API -pthread -Wno-missing-braces -Wno-missing-field-initializers

doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o: .var.out := doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o
doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o: .var.in := /opt/mellanox/doca/samples/common.c
doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o: .var.DEPFILE := doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o.d
doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o: .var.DEPFILE_UNQUOTED := doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o.d
doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o: .var.ARGS := -Idoca_compression_local.p -I. -I.. -I/opt/mellanox/doca/applications/common/src -I/opt/mellanox/doca -I/usr/include/json-c -I/opt/mellanox/doca/include -I/opt/mellanox/flexio/include -I/opt/mellanox/dpdk/include/dpdk -I/opt/mellanox/dpdk/include/dpdk/../aarch64-linux-gnu/dpdk -I/usr/include/libnl3 -I/usr/include/glib-2.0 -I/usr/lib/aarch64-linux-gnu/glib-2.0/include -fdiagnostics-color=always -D_FILE_OFFSET_BITS=64 -Wall -Winvalid-pch -g '-D DOCA_ALLOW_EXPERIMENTAL_API' -Wno-format-zero-length '-D DOCA_USE_LIBBSD' '-D RTE_USE_LIBBSD' -include rte_config.h -mcpu=cortex-a72 -DALLOW_EXPERIMENTAL_API -pthread -Wno-missing-braces -Wno-missing-field-initializers

doca_compression_local: .var.out := doca_compression_local
doca_compression_local: .var.in := doca_compression_local.p/compression_local.c.o doca_compression_local.p/_opt_mellanox_doca_applications_common_src_pack.c.o doca_compression_local.p/_opt_mellanox_doca_applications_common_src_utils.c.o doca_compression_local.p/_opt_mellanox_doca_applications_.._samples_common.c.o
doca_compression_local: .var.LINK_ARGS := -Wl,--as-needed -Wl,--no-undefined -Wl,-rpath,/opt/mellanox/doca/lib/aarch64-linux-gnu -Wl,-rpath-link,/opt/mellanox/doca/lib/aarch64-linux-gnu -pthread -Wl,--start-group /usr/lib/aarch64-linux-gnu/libjson-c.so /usr/lib/aarch64-linux-gnu/libbsd.so -Wl,--as-needed /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_common.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_argp.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpdk_bridge.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dma.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_compress.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_telemetry.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_ipsec.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_flow.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_erasure_coding.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_flow_ct.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_comm_channel.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_regex.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_apsh.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_sha.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpa.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_rdma.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_dpi.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_eth.so /opt/mellanox/doca/lib/aarch64-linux-gnu/libdoca_pcc.so /usr/lib/aarch64-linux-gnu/libz.so -Wl,--end-group

build.ninja: .var.out := build.ninja
build.ninja: .var.in := meson.build meson_options.txt meson-private/coredata.dat
build.ninja: .var.pool := console

reconfigure: .var.out := reconfigure
reconfigure: .var.in := PHONY
reconfigure: .var.pool := console

.PHONY: docker-build
docker-image := nvcr.io/nvidia/doca/doca:2.6.0-devel
source-dir := $(CURDIR)
container-dir := /opt/mellanox/doca/applications/local-compress
docker-build:
	docker run --rm --platform=linux/arm64 -v $(source-dir):$(container-dir) -w $(container-dir) $(docker-image) make

.PHONY: docker-build-old
docker-image-old := nvcr.io/nvidia/doca/doca:2.2.0-devel
source-dir := $(CURDIR)
container-dir := /opt/mellanox/doca/applications/local-compress
docker-build-old:
	docker run --rm --platform=linux/arm64 -v $(source-dir):$(container-dir) -w $(container-dir) $(docker-image-old) make

.PHONY: benchmark clean dist install test uninstall