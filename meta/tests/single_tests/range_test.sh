#!/bin/bash

ulimit -c unlimited
export MPICH_MAX_THREAD_SAFETY=multiple

nodes=$SLURM_NNODES
BATCH_CNT=1024

for BULK_NUM in ${BATCH_CNT}; do
	srun --clear-ssd -n${SLURM_NNODES} -N${SLURM_NNODES} ./range_test -c ${BATCH_CNT} -s 1 -t 16384 -r 1048576 -n 131072 -p /l/ssd/ -d db 
done

