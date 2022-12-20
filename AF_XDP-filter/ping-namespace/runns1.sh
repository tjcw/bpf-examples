#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Client side helper script to run iperf3 for TCP performance testing
# in a namespace with the eBPF filter
ip link set lo up
ip link set vpeer1 up
ip addr add 10.10.0.10/16 dev vpeer1
if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -i vpeer1 -w vpeer1.tcpdump &
  tcpdump_vpeer1_pid=$!
fi
sleep 6
ping -c 2 10.10.0.20
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_vpeer1_pid}
fi
wait
