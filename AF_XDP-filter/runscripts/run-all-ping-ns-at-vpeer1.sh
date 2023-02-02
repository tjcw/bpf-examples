#!/bin/bash -x
ulimit -c unlimited
(
  cd ping-namespace-at-vpeer1
  echo "ping-namespace-at-vpeer1"
  LEAVE=1 RR=1 PORT=50008 ./run.sh
#  FILTER=af_xdp_kern_passall PORT=50007 ./run.sh
  FILTER=af_xdp_kern_dummy PORT=50009 DUMMY=1 ./run.sh
  FILTER=af_xdp_kern PORT=50006 ./run.sh
  FILTER=af_xdp_kern PORT=50007 BOTH=1 ./run.sh
)
