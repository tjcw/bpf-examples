#!/bin/bash -x
ulimit -c unlimited
(
  cd iperf3-namespace-at-vpeer1
  echo "iperf3-namespace-at-vpeer1"
  LEAVE=1 RR=1 PORT=50008 ./run.sh
  FILTER=af_xdp_kern_passall PORT=50009 ./run.sh
  FILTER=af_xdp_kern_dummy PORT=50010 DUMMY=1 ./run.sh
  SWCKSUM=1 FILTER=af_xdp_kern PORT=50011 ./run.sh
  SWCKSUM=1 FILTER=af_xdp_kern PORT=50012 BOTH=1 ./run.sh
  SWCKSUM=1 FILTER=af_xdp_kern PORT=50013 ALL_PACKETS=1 ./run.sh
  SWCKSUM=1 FILTER=af_xdp_kern PORT=50014 BOTH=1 ALL_PACKETS=1 ./run.sh
)
