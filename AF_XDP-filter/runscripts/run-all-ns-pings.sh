#!/bin/bash -x
ulimit -c unlimited
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
  cd ping-namespace-direct-at-veth1
  FILTER=af_xdp_kern ./run.sh
)
(
  cd ping-namespace-direct-with-dummy-at-bridge
  FILTER=af_xdp_kern ./run.sh
)
