#!/bin/bash

# this is a unit speed test.  It creates a randomized file
# on the parallel file system.  Then it uses the transfer
# API to transfer that file into the UnifyFS space, then
# then transfers it back out again to a *different* location.
# The validation test is to compare the final file to the
# original file to make sure they're bit by bit identical.

# env variable STAGE_FILE_SIZE_IN_MB sets the size
# this has a default below of 100 MB

# As long as UNIFY_LOG_DIR is set this script will have
# places to put its temp files.  This includes successive
# of this script internally keeping track of timing
# iteration indices.

# These two variables are written to every timing data line:
# the variable STAGE_TEST_OVERALL_CONFIG is used to tag a whole set
# of tests.
# the variable STAGE_TEST_SPECIFIC_CONFIG tags lower-level config
# info, typically what's changing in the overall set of tests
# file size is taken care of, so the overall config variable
# will typically be what machine or software is being tested.
# The specific config might be a specific server configuration
# that's being checked or iterated over.

test_description="UnifyFS Stage-in+Stage-out tests"

# utility checking to make sure everything's in place.
STAGE_EXE=${UNIFYFS_BIN}/unifyfs-stage
test_expect_success "unify-stage executable exists" '
     test_path_is_file $STAGE_EXE
'
test_expect_success "\$UNIFYFS_LOG_DIR exists" '
    test_path_is_dir $UNIFYFS_LOG_DIR
'

# make and check utility directories
STAGE_CFG_DIR="${UNIFYFS_LOG_DIR}/stage/config_800"
STAGE_LOG_DIR="${UNIFYFS_LOG_DIR}/stage/log_800"
STAGE_SRC_DIR="${UNIFYFS_LOG_DIR}/stage/stage_source_800"
STAGE_DST_DIR="${UNIFYFS_LOG_DIR}/stage/stage_destination_800"
mkdir -p $STAGE_CFG_DIR $STAGE_LOG_DIR $STAGE_SRC_DIR $STAGE_DST_DIR

test_expect_success "stage testing dirs exist" '
    test_path_is_dir  $STAGE_CFG_DIR &&
    test_path_is_dir  $STAGE_LOG_DIR &&
    test_path_is_dir  $STAGE_SRC_DIR &&
    test_path_is_dir  $STAGE_DST_DIR
'

# keeping track of where we are in iterations to *guarantee* even
# with weird iteration counts that the iter counter will always
# be unique and thus preserve data.
STAGE_TEST_INDEX_FILE="${STAGE_LOG_DIR}/stage_test_index.txt"
# ensure existence of index file
# index should consist of a number
if ! [ -e "$STAGE_TEST_INDEX_FILE" ] ; then
    echo "1" > ${STAGE_TEST_INDEX_FILE}
fi
# the test index file now exists
STAGE_TEST_INDEX=`head -n1 ${STAGE_TEST_INDEX_FILE}`
NEW_STAGE_TEST_INDEX=$(( STAGE_TEST_INDEX + 1 ))
echo ${NEW_STAGE_TEST_INDEX} > ${STAGE_TEST_INDEX_FILE}
echo "stage index this run: ${STAGE_TEST_INDEX}"

if [ ! $STAGE_FILE_SIZE_IN_MB ] ; then
    STAGE_FILE_SIZE_IN_MB=100
fi
echo "stage file test: file size is $STAGE_FILE_SIZE_IN_MB in MB"
STAGE_FILE_CFG=${STAGE_FILE_SIZE_IN_MB}MB_${STAGE_TEST_INDEX}

# empty comment so format checker doesn't complain about next line
STAGE_SRC_FILE=${STAGE_SRC_DIR}/source_800_${STAGE_FILE_CFG}.file

# fill initial file with random bytes
dd if=/dev/urandom bs=1M count=${STAGE_FILE_SIZE_IN_MB} of=${STAGE_SRC_FILE}

test_expect_success "source.file exists" '
    test_path_is_file $STAGE_SRC_FILE
'

rm -f ${STAGE_CFG_DIR}/*
rm -f ${STAGE_DST_DIR}/*

test_expect_success "destination directory is empty" '
    test_dir_is_empty $STAGE_DST_DIR
'

##### Serial unifyfs-stage tests #####

# set up what the intermediate filename will be within UnifyFS and also
# the final file name after copying it back out.  Then use those
# filenames to create the two manifest files, one for copying the
# file in, and one for copying the file out.
STAGE_IM_FILE=${UNIFYFS_MP}/intermediate_${STAGE_FILE_CFG}.file
STAGE_DST_FILE=${STAGE_DST_DIR}/destination_800_${STAGE_FILE_CFG}.file
MAN_IN=${STAGE_CFG_DIR}/stage_IN_${STAGE_FILE_CFG}.manifest
MAN_OUT=${STAGE_CFG_DIR}/stage_OUT_${STAGE_FILE_CFG}.manifest
echo "\"${STAGE_SRC_FILE}\" \"${STAGE_IM_FILE}\"" > ${MAN_IN}
echo "\"${STAGE_IM_FILE}\" \"${STAGE_DST_FILE}\"" > ${MAN_OUT}

test_expect_success "config directory now has manifest files" '
    test_path_is_file  $MAN_IN &&
    test_path_is_file  $MAN_OUT
'

STAGE_IN_ERR=${STAGE_LOG_DIR}/stage_IN_800_${STAGE_FILE_CFG}.err
STAGE_OUT_ERR=${STAGE_LOG_DIR}/stage_OUT_800_${STAGE_FILE_CFG}.err

STAGE_IN_STATUS=${STAGE_LOG_DIR}/stage_IN_800_${STAGE_FILE_CFG}.status
STAGE_OUT_STATUS=${STAGE_LOG_DIR}/stage_OUT_800_${STAGE_FILE_CFG}.status

# run and time the stage-in operation.
STAGEIN_COMMAND="${JOB_RUN_COMMAND} $app_err $STAGE_IN_ERR $STAGE_EXE -S ${STAGE_IN_STATUS} -m ${UNIFYFS_MP} -v -c  ${MAN_IN}"
echo "stagein_command: ${STAGEIN_COMMAND}"
TIME_IN_START=`date +%s`
my_stagein_output="$($STAGEIN_COMMAND)"
TIME_IN_END=`date +%s`

# run and time the stage-out operation.
STAGEOUT_COMMAND="${JOB_RUN_COMMAND} $app_err $STAGE_OUT_ERR $STAGE_EXE -S ${STAGE_OUT_STATUS} -m ${UNIFYFS_MP} -v -c ${MAN_OUT}"
echo "stageOUT_command: ${STAGEOUT_COMMAND}"
TIME_OUT_START=`date +%s`
my_stageout_output="$($STAGEOUT_COMMAND)"
TIME_OUT_END=`date +%s`

STAGE_IN_LOG=${STAGE_LOG_DIR}/stage_IN_800_${STAGE_FILE_CFG}.out
STAGE_OUT_LOG=${STAGE_LOG_DIR}/stage_OUT_800_${STAGE_FILE_CFG}.out
echo $my_stagein_output > $STAGE_IN_LOG
echo $my_stageout_output > $STAGE_OUT_LOG

ELAPSED_TIME_IN=$(( ${TIME_IN_END} - ${TIME_IN_START} ))
ELAPSED_TIME_OUT=$(( ${TIME_OUT_END} - ${TIME_OUT_START} ))
echo "time to stage in: $ELAPSED_TIME_IN s"
echo "time to stage out: $ELAPSED_TIME_OUT s"

test_expect_success "input file has been staged to output" '
    test_path_is_file $STAGE_DST_FILE
'

# This block is used to indirectly get the test result back to us
# of whether the file comparison failed, so that we can put
# that result in the line of the timing file.
SUCCESS_TOTAL_BEFORE=${test_success}
test_expect_success "final output is identical to initial input" '
    test_cmp $STAGE_DST_FILE $STAGE_SRC_FILE
'
SUCCESS_TOTAL_AFTER=${test_success}

# If the success total is *different*, then the final test
# (the file comparison after to before) passed.
# If they're the same, then it failed.
if [ ${SUCCESS_TOTAL_BEFORE} == ${SUCCESS_TOTAL_AFTER} ]; then
    echo "${STAGE_TEST_INDEX} ${STAGE_FILE_SIZE_IN_MB} \
      ${ELAPSED_TIME_IN} ${ELAPSED_TIME_OUT}          \
      ^${STAGE_TEST_OVERALL_CONFIG}^                  \
      @${STAGE_TEST_SPECIFIC_CONFIG}@ %FAIL%"         \
      >> ${STAGE_LOG_DIR}/timings_serial_${JOB_ID}.dat
else
    echo "${STAGE_TEST_INDEX} ${STAGE_FILE_SIZE_IN_MB} \
      ${ELAPSED_TIME_IN} ${ELAPSED_TIME_OUT}          \
      ^${STAGE_TEST_OVERALL_CONFIG}^                  \
      @${STAGE_TEST_SPECIFIC_CONFIG}@ %GOOD%"         \
      >> ${STAGE_LOG_DIR}/timings_serial_${JOB_ID}.dat
fi


##### Parallel unifyfs-stage tests #####

# set up what the intermediate filename will be within UnifyFS and also
# the final file name after copying it back out.  Then use those
# filenames to create the two manifest files, one for copying the
# file in, and one for copying the file out.
STAGE_P_IM_FILE=${UNIFYFS_MP}/intermediate_parallel_${STAGE_FILE_CFG}.file
STAGE_P_DST_FILE=${STAGE_DST_DIR}/destination_parallel_800_${STAGE_FILE_CFG}.file
MAN_IN=${STAGE_CFG_DIR}/stage_IN_parallel_${STAGE_FILE_CFG}.manifest
MAN_OUT=${STAGE_CFG_DIR}/stage_OUT_parallel_${STAGE_FILE_CFG}.manifest
echo "\"${STAGE_SRC_FILE}\" \"${STAGE_P_IM_FILE}\"" > ${MAN_IN}
echo "\"${STAGE_P_IM_FILE}\" \"${STAGE_P_DST_FILE}\"" > ${MAN_OUT}

test_expect_success "config directory now has parallel manifest files" '
    test_path_is_file  $MAN_IN &&
    test_path_is_file  $MAN_OUT
'

STAGE_IN_ERR=${STAGE_LOG_DIR}/stage_IN_parallel_800_${STAGE_FILE_CFG}.err
STAGE_OUT_ERR=${STAGE_LOG_DIR}/stage_OUT_parallel_800_${STAGE_FILE_CFG}.err

STAGE_IN_STATUS=${STAGE_LOG_DIR}/stage_IN_parallel_800_${STAGE_FILE_CFG}.status
STAGE_OUT_STATUS=${STAGE_LOG_DIR}/stage_OUT_parallel_800_${STAGE_FILE_CFG}.status

# run and time the stage-in operation.
STAGEIN_COMMAND="${JOB_RUN_COMMAND} $app_err $STAGE_IN_ERR $STAGE_EXE -S ${STAGE_IN_STATUS} -m ${UNIFYFS_MP} -v -c -p ${MAN_IN}"
echo "stagein_command: ${STAGEIN_COMMAND}"
TIME_IN_START=`date +%s`
my_stagein_output="$($STAGEIN_COMMAND)"
TIME_IN_END=`date +%s`

# run and time the stage-out operation.
STAGEOUT_COMMAND="${JOB_RUN_COMMAND} $app_err $STAGE_OUT_ERR $STAGE_EXE -S ${STAGE_OUT_STATUS} -m ${UNIFYFS_MP} -v -c -p ${MAN_OUT}"
echo "stageOUT_command: ${STAGEOUT_COMMAND}"
TIME_OUT_START=`date +%s`
my_stageout_output="$($STAGEOUT_COMMAND)"
TIME_OUT_END=`date +%s`

STAGE_IN_LOG=${STAGE_LOG_DIR}/stage_IN_parallel_800_${STAGE_FILE_CFG}.out
STAGE_OUT_LOG=${STAGE_LOG_DIR}/stage_OUT_parallel_800_${STAGE_FILE_CFG}.out
echo $my_stagein_output > $STAGE_IN_LOG
echo $my_stageout_output > $STAGE_OUT_LOG

ELAPSED_TIME_IN=$(( ${TIME_IN_END} - ${TIME_IN_START} ))
ELAPSED_TIME_OUT=$(( ${TIME_OUT_END} - ${TIME_OUT_START} ))
echo "time to stage in parallel: $ELAPSED_TIME_IN s"
echo "time to stage out parallel: $ELAPSED_TIME_OUT s"

test_expect_success "parallel: input file has been staged to output" '
    test_path_is_file $STAGE_P_DST_FILE
'

# This block is used to indirectly get the test result back to us
# of whether the file comparison failed, so that we can put
# that result in the line of the timing file.
SUCCESS_TOTAL_BEFORE=${test_success}
test_expect_success "parallel: final output is identical to initial input" '
    test_cmp $STAGE_P_DST_FILE $STAGE_SRC_FILE
'
SUCCESS_TOTAL_AFTER=${test_success}

# If the success total is *different*, then the final test
# (the file comparison after to before) passed.
# If they're the same, then it failed.
if [ ${SUCCESS_TOTAL_BEFORE} == ${SUCCESS_TOTAL_AFTER} ]; then
    echo "${STAGE_TEST_INDEX} ${STAGE_FILE_SIZE_IN_MB} \
      ${ELAPSED_TIME_IN} ${ELAPSED_TIME_OUT}          \
      ^${STAGE_TEST_OVERALL_CONFIG}^                  \
      @${STAGE_TEST_SPECIFIC_CONFIG}@ %FAIL%"         \
      >> ${STAGE_LOG_DIR}/timings_parallel_${JOB_ID}.dat
else
    echo "${STAGE_TEST_INDEX} ${STAGE_FILE_SIZE_IN_MB} \
      ${ELAPSED_TIME_IN} ${ELAPSED_TIME_OUT}          \
      ^${STAGE_TEST_OVERALL_CONFIG}^                  \
      @${STAGE_TEST_SPECIFIC_CONFIG}@ %GOOD%"         \
      >> ${STAGE_LOG_DIR}/timings_parallel_${JOB_ID}.dat
fi

test_expect_success "serial output is identical to parallel output" '
    test_cmp $STAGE_DST_FILE $STAGE_P_DST_FILE
'

rm -f $STAGE_SRC_FILE
