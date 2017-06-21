#!/bin/bash

set -x

ulimit -c unlimited
export MPICH_MAX_THREAD_SAFETY=multiple

nnodes=1
nprocs=1
# determine number of nodes in our allocation
nodes=$SLURM_NNODES
SEGNUM=1024
BULKNUM=$SEGNUM
srun --clear-ssd -n${nprocs} -N${nnodes} ./range_bget -c ${BULKNUM} -s 16 -t 1048576 -g 1048576 -r 1048576 -n ${SEGNUM} -p /l/ssd/ -d db 2>&1|tee range_${SLURM_NNODES}.log 

