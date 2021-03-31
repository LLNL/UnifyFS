#!/bin/bash

# this is a unit speed test.  It creates a randomized file
# on the parallel file system.  Then it uses the transfer
# API to transfer that file into the UnifyFS space, then
# then transfers it back out again to a *different* location.
# The validation test is to compare the final file to the
# original file to make sure they're bit by bit identical.

# env variable TEST_FILE_SIZE_IN_MB sets the size
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
test_expect_success "unify-stage exists" '
     test_path_is_file ${UNIFYFS_EXAMPLES}/unifyfs-stage
'
test_expect_success "testing temp dir exists" '
    test_path_is_dir  ${UNIFYFS_LOG_DIR}
'

# make and check utility directories
MY_CONFIG_DIR="${UNIFYFS_LOG_DIR}/config_200"
MY_LOG_DIR="${UNIFYFS_LOG_DIR}/log_200"
MY_SOURCE_DIR="${UNIFYFS_LOG_DIR}/stage_source_200"
MY_DEST_DIR="${UNIFYFS_LOG_DIR}/stage_destination_200"
mkdir -p ${MY_CONFIG_DIR}
mkdir -p ${MY_LOG_DIR}
mkdir -p ${MY_SOURCE_DIR}
mkdir -p ${MY_DEST_DIR}

test_expect_success "stage testing dirs exist" '
    test_path_is_dir  ${MY_CONFIG_DIR} &&
    test_path_is_dir  ${MY_LOG_DIR} &&
    test_path_is_dir  ${MY_SOURCE_DIR} &&
    test_path_is_dir  ${MY_DEST_DIR}
'

# keeping track of where we are in iterations to *guarantee* even
# with weird iteration counts that the iter counter will always
# be unique and thus preserve data.
STAGE_TEST_INDEX_FILENAME="${MY_LOG_DIR}/stage_test_index.txt"
# ensure existence of index file
# index should consist of a number
if ! [ -e "$STAGE_TEST_INDEX_FILENAME" ] ; then
    echo "1" > ${STAGE_TEST_INDEX_FILENAME}
fi
# the test index file now exists
STAGE_TEST_INDEX=`head -n1 ${STAGE_TEST_INDEX_FILENAME}`
NEW_STAGE_TEST_INDEX=`printf %d $((STAGE_TEST_INDEX+1))`
echo ${NEW_STAGE_TEST_INDEX} > ${STAGE_TEST_INDEX_FILENAME}
echo "stage index this run: ${STAGE_TEST_INDEX}"

if [ ! $TEST_FILE_SIZE_IN_MB ] ; then
    TEST_FILE_SIZE_IN_MB=100
fi
echo "stage file test: file size is $TEST_FILE_SIZE_IN_MB in MB"

# empty comment so format checker doesn't complain about next line
MY_INPUT_FILE=${MY_SOURCE_DIR}/source_200_size${TEST_FILE_SIZE_IN_MB}_${STAGE_TEST_INDEX}.file

# fill initial file with random bytes
dd if=/dev/urandom bs=1M count=${TEST_FILE_SIZE_IN_MB} of=${MY_INPUT_FILE}

test_expect_success "source.file exists" '
    test_path_is_file ${MY_INPUT_FILE}
'

rm -f ${MY_CONFIG_DIR}/*
rm -f ${MY_DEST_DIR}/*

test_expect_success "destination directory is empty" '
    test_dir_is_empty ${MY_DEST_DIR}
'

# set up what the intermediate filename will be within UnifyFS and also
# the final file name after copying it back out.  Then use those
# filenames to create the two manifest files, one for copying the
# file in, and one for copying the file out.
FINAL_OUTPUT_FILE=${MY_DEST_DIR}/destination_200_${TEST_FILE_SIZE_IN_MB}_${STAGE_TEST_INDEX}.file
MAN_IN=${MY_CONFIG_DIR}/stage_IN_${TEST_FILE_SIZE_IN_MB}_${STAGE_TEST_INDEX}.manifest
MAN_OUT=${MY_CONFIG_DIR}/stage_OUT_${TEST_FILE_SIZE_IN_MB}_${STAGE_TEST_INDEX}.manifest
echo "\"${MY_INPUT_FILE}\"                                   \
     \"${UNIFYFS_MP}/intermediate_${STAGE_TEST_INDEX}.file\""  > ${MAN_IN}
echo "\"${UNIFYFS_MP}/intermediate_${STAGE_TEST_INDEX}.file\" \
     \"${FINAL_OUTPUT_FILE}\"" > ${MAN_OUT}

test_expect_success "config directory now has manifest files" '
    test_path_is_file  ${MAN_IN} &&
    test_path_is_file  ${MAN_OUT}
'

# run and time the stage-in operation.
STAGEIN_COMMAND="${JOB_RUN_COMMAND} $app_err ${UNIFYFS_LOG_DIR}/${JOB_ID}_200stagein_00_${STAGE_TEST_INDEX}.err ${UNIFYFS_EXAMPLES}/unifyfs-stage -m ${UNIFYFS_MP} ${MAN_IN}"
echo "stagein_command: ${STAGEIN_COMMAND}"
TIME_IN_START=`date +%s`
my_stagein_output="$($STAGEIN_COMMAND)"
TIME_IN_END=`date +%s`

# run and time the stage-out operation.
STAGEOUT_COMMAND="${JOB_RUN_COMMAND} $app_err ${UNIFYFS_LOG_DIR}/${JOB_ID}_200stageout_02_${STAGE_TEST_INDEX}.err ${UNIFYFS_EXAMPLES}/unifyfs-stage -m ${UNIFYFS_MP} ${MAN_OUT}"
echo "stageOUT_command: ${STAGEOUT_COMMAND}"
TIME_OUT_START=`date +%s`
my_stageout_output="$($STAGEOUT_COMMAND)"
TIME_OUT_END=`date +%s`
echo $my_stagein_output > \
 ${UNIFYFS_LOG_DIR}/${JOB_ID}_200stagein_command_01$_${STAGE_TEST_INDEX}.err
echo $my_stageout_output > \
 ${UNIFYFS_LOG_DIR}/${JOB_ID}_200stageout_command_03_${STAGE_TEST_INDEX}.err

ELAPSED_TIME_IN=$(( ${TIME_IN_END} - ${TIME_IN_START} ))
ELAPSED_TIME_OUT=$(( ${TIME_OUT_END} - ${TIME_OUT_START} ))
echo "time to stage in: $ELAPSED_TIME_IN s"
echo "time to stage out: $ELAPSED_TIME_OUT s"

test_expect_success "input file has been staged to output" '
    test_path_is_file ${FINAL_OUTPUT_FILE}
'

# This block is used to indirectly get the test result back to us
# of whether the file comparison failed, so that we can put
# that result in the line of the timing file.
SUCCESS_TOTAL_BEFORE=${test_success}
test_expect_success "final output is identical to initial input" '
    test_cmp ${FINAL_OUTPUT_FILE} ${MY_INPUT_FILE}
'
SUCCESS_TOTAL_AFTER=${test_success}

# If the success total is *different*, then the final test
# (the file comparison after to before) passed.
# If they're the same, then it failed.
if [ ${SUCCESS_TOTAL_BEFORE} == ${SUCCESS_TOTAL_AFTER} ]; then
    echo "${STAGE_TEST_INDEX} ${TEST_FILE_SIZE_IN_MB} \
      ${ELAPSED_TIME_IN} ${ELAPSED_TIME_OUT}          \
      ^${STAGE_TEST_OVERALL_CONFIG}^                  \
      @${STAGE_TEST_SPECIFIC_CONFIG}@ %FAIL%"         \
      >> ${UNIFYFS_LOG_DIR}/timings_${JOB_ID}.dat
else
    echo "${STAGE_TEST_INDEX} ${TEST_FILE_SIZE_IN_MB} \
      ${ELAPSED_TIME_IN} ${ELAPSED_TIME_OUT}          \
      ^${STAGE_TEST_OVERALL_CONFIG}^                  \
      @${STAGE_TEST_SPECIFIC_CONFIG}@ %GOOD%"         \
      >> ${UNIFYFS_LOG_DIR}/timings_${JOB_ID}.dat     \
fi

# nothing to output or return because the results went to the
# timing file.

#  
