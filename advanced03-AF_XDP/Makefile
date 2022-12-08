# SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)


XDP_TARGETS  := af_xdp_kern
USER_TARGETS := af_xdp_user
USER_TARGETS_OBJS := common_params.o common_user_bpf_xdp.o
USER_LIBS := -lxdp -lbpf -lz

LIBBPF_DIR = ../libbpf/src/
COMMON_DIR = ../common/
LIB_DIR = ../lib/

include $(LIB_DIR)/common.mk
CFLAGS += -lpthread
LDFLAGS += -lpthread
