#!/bin/bash -x
(
  cd netperf-real
  echo "netperf-real"
  FILTER=af_xdp_kern TCP_RR=1 PORT=50003 ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 PORT=50004 ./run.sh
  LEAVE=1 TCP_RR=1 PORT=50005 ./run.sh
)
(
  cd iperf3-real
  echo "iperf3-real"

  FILTER=af_xdp_kern TCP_RR=1 PORT=50009  ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 PORT=50010 ./run.sh
  LEAVE=1 TCP_RR=1 PORT=50011 ./run.sh
)
