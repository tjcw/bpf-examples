#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Server side helper script for TCP performance testing with eBPF filter
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set TCPDUMP env var non-null to take tcpdumps of the interfaces
ip link set lo up
ip link set vpeer2 up
ip addr add 10.10.0.20/16 dev vpeer2
ip link set dev vpeer2 xdpgeneric off
if [[ -n "${SWCKSUM}" ]]
then
  ethtool -K vpeer2 tx off
fi
if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -v -i vpeer2 -w vpeer2.tcpdump not ip6 &
  tcpdump_vpeer2_pid=$!
fi
if [[ -n "${BOTH}" ]]
then
  if [[ -n "${INGRESS}" ]]
  then
    mount -t bpf bpf /sys/fs/bpf
    if [[ -z "${ALL_PACKETS}" ]]
    then
      ../../af_xdp_user -S -d vpeer2 -Q 1 --filename ../../${FILTER}.o -r veth2 -a ${ROOTNSPID} &
      af_pid=$!
    else
      ../../af_xdp_user_all_packets -S -d vpeer2 -Q 1 --filename ../../${FILTER}.o -r veth2 -a ${ROOTNSPID} &
      af_pid=$!
    fi
  fi
fi

iperf3 -s -p ${PORT} &
iperf3_pid=$!
sleep 80
kill -INT ${iperf3_pid}
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_vpeer2_pid}
fi
if [[ -n "${BOTH}" ]]
then
  if [[ -n "${INGRESS}" ]]
  then
    kill ${af_pid}
  fi
fi
wait
