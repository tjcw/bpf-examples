This directory contains directories each of which controls a particular test configuration, and contains scripts which sequence through test configurations.

The scripts in this directory set various environment varaibles which are used to control the scripts in the subdirectories. These are :
- LEAVE : set this to cause no interception of packets, for baseline tests
- TCPDUMP : set this to cause tcpdump to be activated on the interfaces
- FILTER : set this to the eBPF filter to use, af_xdp_kern or af_xdp_kern_dummy or af_xdp_kern_passall
- SWCKSUM : set this to cause kernel software checksum of all TCP packets
- PORT : Set this to the TCP server port to use for the test
- BOTH : Set this for filtration on the client namespace as well as the server namespace
- ALL_PACKETS : Set this to cause all packets for a flow to be redirected to userspace
- INGRESS : Set this to filter ingress packets as well as egress packets. Not working at present.

Directories : 

iperf3-namespace  iperf3 between 2 namespaces with intercept at vpeer2 and reinject using tun

iperf3-namespace-at-vpeer1 iperf3 between 2 namespaces with intercept at vpeer1 and reinject at veth1 (work in progress)

iperf3-namespace-at-vpeer1-reverse iperf3 between 2 namespaces with intercept at veth1 and reinject at vpeer1 (work in progress)

iperf3-namespace-direct  iperf3 between 2 namespaces with intercept at veth1 and reinject at bridge

iperf3-real       iperf3 between 2 real machines with reinject using tun

iperf3-real-namespace  iperf3 between 2 real machines with iperf3 in namespaces (work in progress)

netperf-namespace  netperf between 2 namespaces with intercept at vpeer2 and reinject using tun

netperf-namespace-direct netperf between 2 namespaces with intercept at veth2 and reinject at vpeer2

netperf-real      netperf beween 2 real machines with reinject using tun

netperf-vm        netperf between 2 virtual machines with reinject using tun

ping-namespace    ping between 2 namespaces with intercept at vpeer2 and reinject using tun

ping-namespace-direct ping between 2 namespaces with intercept at veth1 and reinject at vpeer2

ping-namespace-direct-at-veth1 ping between 2 namespaces with intercept at veth1 and reinject at veth1. This test fails.

ping-namespace-direct-with-dummy ping between 2 namespaces with intercept at veth1, reinject at veth2, and dummy eBPF program on veth2.

ping-namespace-direct-with-dummy-at-bridge ping between 2 namespaces with intercept at veth1, reinject at bridge, and dummy eBPF program on bridge.

ping-namespace-direct-with-dummy-at-source ping between 2 namespaces with intercept at veth1, reinject at vpeer1, and dummy eBPF program on vpeer1.

run/run.sh setup for running on real machine with reinject using tun

run/runnest.sh setup for running on nested VMs with reinject using tun

run/runns.sh 10 pings between 2 namespaces with intercept at vpeer2 and reinject using tun

run/runvm.sh setup for running beween 2 VMs with reinject using tun

udp-namespace-direct-with-dummy udp test between 2 namespaces with intercept at veth1 and reinject at veth2

Tests which reinject at the TUN interface do not run at the moment, because driving TUN is compiled out in af_xdp_user.c . See k_enable_tun in af_xdp_user.c if you want to compile 
this support in.

Scripts : 
run-all-iperf-ns-at-vpeer1-reverse.sh  Run all configurations in iperf3-namespace-at-vpeer1-reverse

run-all-iperf-ns-at-vpeer1.sh  Run all configurations in iperf3-namespace-at-vpeer1

run-all-iperf-real.sh Run all iperf3 configirations between real machines

run-all-ns-pings.sh  Run all ping tests between naemspaces]

run-all-ping-ns-at-vpeer1.sh  Run all ping tests between namespaces which reunject at at vpeer1

run-all-real.sh  Runa all configrations between real machines