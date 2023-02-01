#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Run script for eBPF TCP performance testing between 2 real machines
# Assumes
#  CLIENT_IP=192.168.17.9 reaches the client machine on a secondary network
#  SERVER_IP=10.1.0.2 reaches from the client to the server on the network under test
#  TUN_IP=10.1.0.254 is the IP address to be assigned to the tun injection device 
ulimit -c unlimited
export CLIENT_IP=192.168.17.9
export SERVER_IP=10.1.0.2
export TUN_IP=10.1.0.254
(
  cd iperf3-real
  echo "iperf3-real"

  FILTER=af_xdp_kern TCP_RR=1 PORT=50009  ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 PORT=50010 ./run.sh
  LEAVE=1 TCP_RR=1 PORT=50011 ./run.sh
  DUMMY=1 FILTER=af_xdp_kern_dummy TCP_RR=1 PORT=50012 ./run.sh
)
