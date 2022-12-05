#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Client side helper script to run iperf3 for TCP performance testing
# in a namespace with the eBPF filter
ip link set lo up
ip link set vpeer1 up
ip addr add 10.10.0.10/16 dev vpeer1
sleep 6
netperf -4 -t TCP_RR -H 10.10.0.20 -p 50000
netperf -4 -t TCP_CRR -H 10.10.0.20 -p 50000
