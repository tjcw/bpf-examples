#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Run script for iperf3 throughput performance test between namespaces
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set PORT to choose a port for the server to listen on

ip netns delete veth1
ip netns delete veth2

ip netns delete ns1
ip netns delete ns2
sleep 2

ip netns add ns1
ip netns add ns2

ip link add veth1 type veth peer name vpeer1
ip link add veth2 type veth peer name vpeer2

ip link set veth1 up
ip link set veth2 up

ip link set vpeer1 netns ns1
ip link set vpeer2 netns ns2

ip link add br0 type bridge
ip link set br0 up

ip link set veth1 master br0
ip link set veth2 master br0

ip addr add 10.10.0.1/16 dev br0

iptables -P FORWARD ACCEPT
iptables -F FORWARD


if [[ -z "${LEAVE}" ]]
then 
  destination_mac=$(ip a s dev br0|awk '{ if($1 == "link/ether") { print $2 } }')
  source_mac=$(ip a s dev veth2|awk '{ if($1 == "link/ether") { print $2 } }')
  DST_MAC=${destination_mac} SRC_MAC=${source_mac} ../../af_xdp_user -S -d veth1 -Q 1 --filename ../../${FILTER}.o -a 1 -r br0 &
  af_pid=$!
  sleep 2
  ../../filter-xdp_stats &
  filter_pid=$!
fi
ip netns exec ns2 ./runns2.sh &
ip netns exec ns1 ./runns1.sh
sleep 60
if [[ -z "${LEAVE}" ]]
then 
  kill -TERM ${af_pid} ${filter_pid}
  wait
fi
