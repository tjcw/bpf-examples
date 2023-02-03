#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Client side helper script to run ping forfinctional testing
# in a namespace with the eBPF filter
ip link set lo up
ip link set vpeer2 up
ip addr add 10.10.0.20/16 dev vpeer2
if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -i vpeer2 -w vpeer2.tcpdump not ip6 &
  tcpdump_vpeer1_pid=$!
fi
sleep 6
iperf3 -c 10.10.0.10 -t 60 -p ${PORT}
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_vpeer2_pid}
fi
wait
