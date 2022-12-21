#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Client side helper script to run ping forfinctional testing
# in a namespace with the eBPF filter
ip link set lo up
ip link set vpeer1 up
ip addr add 10.10.0.10/16 dev vpeer1
if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -i vpeer1 -w vpeer1.tcpdump not ip6 &
  tcpdump_vpeer1_pid=$!
fi
sleep 6
../udp-sender-count 10.10.0.20 50000 2
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_vpeer1_pid}
fi
wait
