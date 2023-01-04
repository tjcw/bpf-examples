#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Run script for ping functional between namespaces
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set TCPDUMP env var non-null if you want take tcpdumps of packets on the interfaces
ulimit -c unlimited
ip link delete veth1
ip link delete veth2

ip netns delete ns1
ip netns delete ns2

rm -f vpeer1.tcpdump vpeer2.tcpdump veth1.tcpdump veth2.tcpdump br0.tcpdump
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

if [[ -n "${TCPDUMP}" ]]
then
  tcpdump -v -i veth1 -w veth1.tcpdump not ip6 &
  tcpdump_veth1_pid=$!
  tcpdump -v -i veth2 -w veth2.tcpdump not ip6 &
  tcpdump_veth2_pid=$!
  tcpdump -v -i br0 -w br0.tcpdump not ip6 &
  tcpdump_br0_pid=$!
  sleep 2
fi
(
  ip netns exec ns2 ./runns2.sh &
  ns2_pid=$!
  sleep 1
  ip netns exec ns1 ./runns1.sh &
  rm -f /sys/fs/bpf/accept_map /sys/fs/bpf/xdp_stats_map
  if [[ -z "${LEAVE}" ]]
  then 
    for device in /proc/sys/net/ipv4/conf/*
    do
      echo 0 >${device}/rp_filter
    done
    iptables -A INPUT -j LOG
    destination_mac=$(ip netns exec ns2 ip a s dev vpeer2|awk '{ if($1 == "link/ether") { print $2 } }')
    source_mac=$(ip netns exec ns1 ip a s dev vpeer1|awk '{ if($1 == "link/ether") { print $2 } }')
## Give a shell prompt here so I can explore what MAC addresses really need giving to af_xdp_user
#    bash
    DST_MAC=${destination_mac} SRC_MAC=${source_mac} ../../af_xdp_user -S -d veth1 -Q 1 --filename ../../${FILTER}.o -r veth1 -a 1 &
    af_pid=$!
    sleep 2
    ../../af_xdp_user_dummy -S -d veth2 -Q 1 --filename ../../af_xdp_kern_dummy.o &
    af_pid_dummy=$!
    sleep 2
    ../../filter-xdp_stats &
    filter_pid=$!
    sleep 120
    iptables -F INPUT
    kill -INT ${af_pid} ${af_pid_dummy}
    kill -TERM ${filter_pid}
  fi 
  wait

)
if [[ -n "${TCPDUMP}" ]]
then
  kill -INT ${tcpdump_veth1_pid} ${tcpdump_veth2_pid} ${tcpdump_br0_pid}
  wait
  chown root.root vpeer1.tcpdump vpeer2.tcpdump veth1.tcpdump veth2.tcpdump br0.tcpdump
  tcpdump -v -r veth1.tcpdump
  tcpdump -v -r veth2.tcpdump
  tcpdump -v -r vpeer1.tcpdump
  tcpdump -v -r vpeer2.tcpdump
  tcpdump -v -r br0.tcpdump
fi
dmesg
