# SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)


SYSTEM_LIBBPF := y
BPF_TARGETS  := af_xdp_kern af_xdp_kern_passall af_xdp_kern_dummy
USER_TARGETS := af_xdp_user af_xdp_user_dummy filter-xdp_stats udp-sender-count udp-receiver
USER_TARGETS_OBJS := common_params.o common_user_bpf_xdp.o
USER_LIBS := -lxdp -lbpf -lz

LIBBPF_DIR = ../libbpf/src/
COMMON_DIR = ../common/
LIB_DIR = ../lib/

include $(LIB_DIR)/common.mk
CFLAGS += -pthread
LDFLAGS += -pthread
