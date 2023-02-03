#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Client side helper script to run ping forfinctional testing
# in a namespace with the eBPF filter
ip link set lo up
ip link set vpeer1 up
ip addr add 10.10.0.10/16 dev vpeer1
if [[ -n "${SWCKSUM}" ]]
then
  ethtool -K vpeer1 tx off
fi
if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -i vpeer1 -w vpeer1.tcpdump not ip6 &
  tcpdump_vpeer1_pid=$!
fi
if [[ -n "${INGRESS}" ]]
then
  if [[ -z "${ALL_PACKETS}" ]]
  then
    ../../af_xdp_user -S -d vpeer1 -Q 1 --filename ../../${FILTER}.o -r veth1 -a ${ROOTNSPID} &
    af_pid=$!
  else
    ../../af_xdp_user_all_packets -S -d vpeer1 -Q 1 --filename ../../${FILTER}.o -r veth1 -a ${ROOTNSPID} &
    af_pid=$!
  fi
fi
sleep 6
iperf3 -c 10.10.0.20 -t 60 -p ${PORT}
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_vpeer1_pid}
fi
if [[ -n "${INGRESS}" ]]
then
  kill -INT ${af_pid}
fi
wait
