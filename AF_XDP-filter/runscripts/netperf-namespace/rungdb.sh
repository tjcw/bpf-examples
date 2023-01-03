#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Run script for iperf3 throughput performance test between namespaces
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set TCP_RR env var non-null to include TCP_RR test 
# Set TCPDUMP env var non-null if you want take tcpdumps of packets on the interfaces
# Set PORT to indicate the port to use. Successive test shouuld have different port numbers because netserver leaves the old port in TIMED_WAIT
ip link delete veth1
ip link delete veth2

ip netns delete ns1
ip netns delete ns2

rm -f tun0.tcpdump vpeer1.tcpdump vpeer2.tcpdump veth1.tcpdump veth2.tcpdump
sleep 2

ip netns add ns1
ip netns add ns2

ip link add veth1 type veth peer name vpeer1
ip link add veth2 type veth peer name vpeer2

ip link set veth1 up
ip link set veth2 up

ip link set vpeer1 netns ns1
ip link set vpeer2 netns ns2

ip tuntap add mode tun tun0
ip link set dev tun0 down
ip addr add 10.10.0.30/24 dev tun0
ip link set dev tun0 up

ip link add br0 type bridge
ip link set br0 up

ip link set veth1 master br0
ip link set veth2 master br0
ip link set tun0 master br0

ip link set dev tun0 netns ns2

ip addr add 10.10.0.1/16 dev br0

iptables -P FORWARD ACCEPT
iptables -F FORWARD

if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -v -i veth1 -w veth1.tcpdump &
  tcpdump_veth1_pid=$!
  tcpdump -v -i veth2 -w veth2.tcpdump &
  tcpdump_veth2_pid=$!
fi
(
  ip netns exec ns2 ./runns2gdb.sh &
  bash
  ip netns exec ns1 ./runns1.sh

  wait
)
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_veth1_pid} ${tcpdump_veth2_pid}
  wait
  chown root.root tun0.tcpdump vpeer1.tcpdump vpeer2.tcpdump veth1.tcpdump veth2.tcpdump 
  tcpdump -v -r tun0.tcpdump
  tcpdump -v -r vpeer1.tcpdump
  tcpdump -v -r vpeer2.tcpdump
  tcpdump -v -r veth1.tcpdump
  tcpdump -v -r veth2.tcpdump
fi

