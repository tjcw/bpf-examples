#!/bin/bash -x
(
  cd netperf-namespace
  echo "netperf-namespace"
  FILTER=af_xdp_kern TCP_RR=1 PORT=50000 ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 PORT=50001 ./run.sh
  LEAVE=1 TCP_RR=1 PORT=50002 ./run.sh
)
#(
#  cd netperf-real
#  echo "netperf-real"
#  FILTER=af_xdp_kern TCP_RR=1 PORT=50003 ./run.sh
#  FILTER=af_xdp_kern_passall TCP_RR=1 PORT=50004 ./run.sh
#  LEAVE=1 TCP_RR=1 PORT=50005 ./run.sh
#)
(
  cd iperf3-namespace
  echo "iperf3-namespace"
  FILTER=af_xdp_kern TCP_RR=1 PORT=50006 ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 PORT=50007 ./run.sh
  LEAVE=1 TCP_RR=1 PORT=50008 ./run.sh
)
#(
#  cd iperf3-real
#  echo "iperf3-real"
#
#  FILTER=af_xdp_kern TCP_RR=1 PORT=50009  ./run.sh
#  FILTER=af_xdp_kern_passall TCP_RR=1 PORT=50010 ./run.sh
#  LEAVE=1 TCP_RR=1 PORT=50011 ./run.sh
#)
(
  cd udp-namespace-direct-with-dummy
  ./teardown.sh
  FILTER=af_xdp_kern PORT=50000 ./runns.sh
)
(
  cd ping-namespace
  FILTER=af_xdp_kern ./run.sh
)
(
  cd ping-namespace-direct
  FILTER=af_xdp_kern ./run.sh
)
(
  cd ping-namespace-direct-with-dummy
  FILTER=af_xdp_kern ./run.sh
)
(
  cd ping-namespace-direct-with-dummy-at-source
  FILTER=af_xdp_kern ./run.sh
)
(
  cd ping-namespace-direct-with-dummy-at-veth1
  FILTER=af_xdp_kern ./run.sh
)
(
  cd ping-namespace-direct-with-dummy-at-bridge
  FILTER=af_xdp_kern ./run.sh
)