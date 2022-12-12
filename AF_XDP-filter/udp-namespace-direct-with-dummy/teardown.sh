#!/bin/bash -x
# SPDX-License-Identifier: GPL-2.0
# Teardown script for UDP functional test between namespaces

ip link delete veth1
ip link delete veth2
ip link delete br0

ip netns delete ns1
ip netns delete ns2

rm -f vpeer1.tcpdump vpeer2.tcpdump veth1.tcpdump veth2.tcpdump br0.tcpdump

