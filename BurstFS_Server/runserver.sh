#!/bin/sh

killall srun ##kill the previous daemon if there is any remaining

export BURSTFS_META_SERVER_RATIO=1 #determines the number of range servers in the key-value store
export BURSTFS_META_DB_NAME=burstfs_db
export CRUISE_CHUNK_MEM=0

procs_per_node=1
nnodes=1
nprocs=`echo "${procs_per_node}*${nnodes}"|bc -l|xargs printf "%1.0f"`
nlist=`scontrol show hostnames ${SLURM_NODELIST}|paste -d, -s|cut -d ',' -f1-${nnodes}`

#clear all the leftovers
#for ((i=1; i<=${nnodes}; i++)); do
#  remote_host=`echo "$nlist"|cut -d ',' -f${i}`
#  rsh ${remote_host} "rm -rf /tmp/burstfs*; rm /dev/shm/*; rm /tmp/mdhim*" #clear the leftovers from previous daemon
#done

ulimit -c unlimited
srun --clear-ssd --nodelist=${nlist} --ntasks-per-node=${procs_per_node}  --distribution=block -n ${nprocs} -N ${nnodes} ./burstfsd 2>&1 |tee server_${nprocs}.log &
