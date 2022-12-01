#!/bin/bash -x
(
  cd netperf-namespace
  echo "netperf-namespace"
  FILTER=af_xdp_kern TCP_RR=1 ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 ./run.sh
  LEAVE=1 TCP_RR=1 ./run.sh
)
(
  cd netperf-real
  echo "netperf-real"
  FILTER=af_xdp_kern TCP_RR=1 ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 ./run.sh
  LEAVE=1 TCP_RR=1 ./run.sh
)
(
  cd iperf3-namespace
  echo "iperf3-namespace"
  FILTER=af_xdp_kern TCP_RR=1 ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 ./run.sh
  LEAVE=1 TCP_RR=1 ./run.sh
)
(
  cd iperf3-real
  echo "iperf3-real"

  FILTER=af_xdp_kern TCP_RR=1 ./run.sh
  FILTER=af_xdp_kern_passall TCP_RR=1 ./run.sh
  LEAVE=1 TCP_RR=1 ./run.sh
)
