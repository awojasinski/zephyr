# Copyright (c) 2020 Intel Corporation
# SPDX-License-Identifier: Apache-2.0

if [ -z "$RUNNING_FROM_MAIN_SCRIPT" ]; then
    echo "Do not run this script directly!"
    echo "Run $ZEPHYR_BASE/scripts/net/run-sample-tests.sh instead."
    exit 1
fi

# Running PTP sample using Docker testing

start_configuration || return $?
start_docker \
	"/usr/local/sbin/ptp4l -2 -f /etc/default.cfg -m -q -l 6 -S -i eth0" \
	|| return $?

# For native_sim PTP run, the delay threshold needs to be huge
start_zephyr "$overlay" "-DCONFIG_NET_SAMPLE_RUN_DURATION=10" \
	         "-DCONFIG_NET_GPTP_NEIGHBOR_PROP_DELAY_THR=12000000"

wait_zephyr
ptp_result=$?

if [ $ptp_result -eq 1 -o $ptp_result -eq 2 ]; then
	result=0
else
	result=1
fi

stop_docker
