iperf3-namespace  iperf3 between 2 namespaces with intercept at vpeer2 and reinject using tun

iperf3-real       iperf3 between 2 real machines with reinject using tun

netperf-namespace  netperf between 2 namespaces with intercept at vpeer2 and reinject using tun

netperf-namespace-direct netperf between 2 namespaces with intercept at veth2 and reinject at vpeer2

netperf-real      netperf beween 2 real machines with reinject using tun

netperf-vm        netperf between 2 virtual machines with reinject using tun

ping-namespace    ping between 2 namespaces with intercept at vpeer2 and reinject using tun

ping-namespace-direct ping between 2 namespaces with intercept at veth1 and reinject at vpeer2

ping-namespace-direct-at-veth1 ping between 2 namespaces with intercept at veth1 and reinject at veth1. This test fails.

ping-namespace-direct-with-dummy ping between 2 namespaces with intercept at veth1, reinject at veth2, and dummy eBPF program on veth2.

ping-namespace-direct-with-dummy-at-bridge ping between 2 namespaces with intercept at veth1, reinject at bridge, and dummy eBPF program on veth2. (Should be on bridge).

ping-namespace-direct-with-dummy-at-source ping between 2 namespaces with intercept at veth1, reinject at vpeer1, and dummy eBPF program on veth2. (Should be on vpeer1)

run/run.sh setup for running on real machine with reinject using tun

run/runnest.sh setup for running on nested VMs with reinject using tun

run/runns.sh 10 pings between 2 namespaces with intercept at vpeer2 and reinject using tun

run/runvm.sh setup for running beween 2 VMs with reinject using tun

udp-namespace-direct-with-dummy udp test between 2 namespaces with intercept at veth1 and reinject at veth2