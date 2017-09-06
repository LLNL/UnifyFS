#!/bin/sh
export CRUISE_USE_SPILLOVER=1
CRUISE_DIR=../BurstFS_Client/tests

procs_per_node=1
nnodes=1
nprocs=`echo "${procs_per_node}*${nnodes}"|bc -l|xargs  printf '%1.0f'`

TO_UNMOUNT=1
PAT=segmented
#PAT=strided
#PAT=n-n
# configure the read transfer size same as the write transfer size, relaxing
# this later
TRAN_SZ=1048576
TRAN_NUM=64
if [ "$PAT" = "segmented" ]; then
  BLK_SZ=`echo "${TRAN_NUM}*${TRAN_SZ}"|bc -l|xargs  printf '%1.0f'`
  SEG_CNT=1
	PAT=0
fi

if [ "$PAT" = "strided" ]; then
  SEG_CNT=${TRAN_NUM}
  BLK_SZ=${TRAN_SZ}
	PAT=0
fi

if [ "$PAT" = "n-n" ]; then
  BLK_SZ=`echo "${TRAN_NUM}*${TRAN_SZ}"|bc -l|xargs  printf '%1.0f'`
  SEG_CNT=1
	PAT=1
fi

nlist=`scontrol show hostnames ${SLURM_NODELIST}|paste -d, -s|cut -d ',' -f1-${nnodes}`

ulimit -c unlimited
srun --drop-caches --nodelist=${nlist} --distribution=block --ntasks-per-node=${procs_per_node} -n ${nprocs} -N ${nnodes} ${CRUISE_DIR}/test_listread -b ${BLK_SZ} -s ${SEG_CNT} -t ${TRAN_SZ} -f /tmp/abc -p ${PAT} -u ${TO_UNMOUNT} 2>&1|tee rclient_${nprocs}.log
