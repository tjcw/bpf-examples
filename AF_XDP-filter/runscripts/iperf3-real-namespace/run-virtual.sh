#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Run script for eBPF TCP performance testing between 2 real machines
# Run this on the server. Assumes server IP address is ${SERVER_IP}
# Assumes client is reachable on another network as ${CLIENT_IP}
# Arranged for a Pensando NIC which uses 16 queues.
# Set FILTER env var to af_xdp_kern or af_xdp_kern_passall according to which filter to use
# Set LEAVE env var non-null for baseline test with no eBPF filter
# Set PORT to choose a port for the server to listen on
# Set CLIENT_IP, SERVER_IP, BRIDGE_IP and SERVER_NODE_IP as required

export FILTER=af_xdp_kern
export LEAVE=1
export PORT=50000
export SERVER_NODE_IP=192.168.123.117
export CLIENT_IP=192.168.123.150
ip link set dev enp1s0 xdpgeneric off
ip addr del ${SERVER_NODE_IP}/24 dev enp1s0
rm -f /sys/fs/bpf/accept_map /sys/fs/bpf/xdp_stats_map

ip link delete veth1

ip netns delete ns1

rm -f vpeer1.tcpdump vpeer2.tcpdump veth1.tcpdump veth2.tcpdump

ip netns add ns1
ip link add veth1 type veth peer name vpeer1
ip link set veth1 up
ip link set vpeer1 netns ns1
ip netns exec ns1 ip addr add ${SERVER_NODE_IP}/24 dev vpeer1
ip netns exec ns1 ip link set vpeer1 up

ip link add br0 type bridge
#ip addr add ${BRIDGE_IP}/24 dev br0
ip link set br0 up

ip link set veth1 master br0
ip link set enp1s0 master br0

#ssh ${CLIENT_IP} ip route add -host ${SERVER_IP} gw ${SERVER_NODE_IP}
#ip netns exec ns1 ip route add default via ${BRIDGE_IP} dev vpeer1
if [[ -z "${LEAVE}" ]]
then 
  for device in /proc/sys/net/ipv4/conf/*
  do
    echo 2 >${device}/rp_filter
  done
  ip netns exec ns1 iperf3 -s -p ${PORT}  &
  iperf3_pid=$!
  sleep 2
  ../../af_xdp_user -S -d veth1 -Q 1 --filename ../../${FILTER}.o -r vpeer1 -a ${iperf3_pid} &
  real_pid=$!
  sleep 2
  ssh ${CLIENT_IP} iperf3 -c ${SERVER_IP} -p ${PORT} | tee client.log
  kill -INT ${iperf3_pid} ${real_pid}
  for device in /proc/sys/net/ipv4/conf/*
  do
    echo 2 >${device}/rp_filter
  done
else
  ip netns exec ns1 iperf3 -s -p ${PORT} &
  iperf3_pid=$!
  ssh ${CLIENT_IP} iperf3 -c ${SERVER_IP} -p ${PORT} | tee client.log
  kill -INT ${iperf3_pid}
fi
wait
#ssh ${CLIENT_IP} ip route del -host ${SERVER_IP} gw ${SERVER_NODE_IP}


