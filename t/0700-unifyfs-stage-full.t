#!/bin/bash
#
# Test unifyfs-stage executable for basic functionality
#

test_description="Test basic functionality of unifyfs-stage executable"

. $(dirname $0)/sharness.sh

test_expect_success "unifyfs-stage exists" '
    test_path_is_file ${SHARNESS_BUILD_DIRECTORY}/util/unifyfs-stage/src/unifyfs-stage
'
test_expect_success "testing temp dir exists" '
    test_path_is_dir  ${UNIFYFS_TEST_TMPDIR}
'

mkdir -p ${UNIFYFS_TEST_TMPDIR}/config_0700
mkdir -p ${UNIFYFS_TEST_TMPDIR}/stage_source
mkdir -p ${UNIFYFS_TEST_TMPDIR}/stage_destination_0700

test_expect_success "stage testing dirs exist" '
    test_path_is_dir  ${UNIFYFS_TEST_TMPDIR}/config_0700
    test_path_is_dir  ${UNIFYFS_TEST_TMPDIR}/stage_source
    test_path_is_dir  ${UNIFYFS_TEST_TMPDIR}/stage_destination_0700
'

dd if=/dev/urandom bs=4M count=1 of=${UNIFYFS_TEST_TMPDIR}/stage_source/source_0700.file

test_expect_success "source.file exists" '
    test_path_is_file ${UNIFYFS_TEST_TMPDIR}/stage_source/source_0700.file
'

rm -f ${UNIFYFS_TEST_TMPDIR}/config_0700/*
rm -f ${UNIFYFS_TEST_TMPDIR}/stage_destination_0700/*

test_expect_success "config_0700 directory is empty" '
    test_dir_is_empty ${UNIFYFS_TEST_TMPDIR}/config_0700
'

echo "\"${UNIFYFS_TEST_TMPDIR}/stage_source/source_0700.file\" \"${UNIFYFS_TEST_MOUNT}/intermediate.file\""  > ${UNIFYFS_TEST_TMPDIR}/config_0700/test_IN.manifest
echo "\"${UNIFYFS_TEST_MOUNT}/intermediate.file\" \"${UNIFYFS_TEST_TMPDIR}/stage_destination_0700/destination_0700.file\"" > ${UNIFYFS_TEST_TMPDIR}/config_0700/test_OUT.manifest

test_expect_success "config_0700 directory now has manifest files" '
    test_path_is_file  ${UNIFYFS_TEST_TMPDIR}/config_0700/test_IN.manifest
    test_path_is_file  ${UNIFYFS_TEST_TMPDIR}/config_0700/test_OUT.manifest
'

test_expect_success "target directory is empty" '
    test_dir_is_empty ${UNIFYFS_TEST_TMPDIR}/stage_destination_0700
'

$JOB_RUN_COMMAND ${SHARNESS_BUILD_DIRECTORY}/util/unifyfs-stage/src/unifyfs-stage -m ${UNIFYFS_TEST_MOUNT} ${UNIFYFS_TEST_TMPDIR}/config_0700/test_IN.manifest > ${UNIFYFS_TEST_TMPDIR}/config_0700/stage_IN_output.OUT 2>&1

$JOB_RUN_COMMAND ${SHARNESS_BUILD_DIRECTORY}/util/unifyfs-stage/src/unifyfs-stage -m ${UNIFYFS_TEST_MOUNT} ${UNIFYFS_TEST_TMPDIR}/config_0700/test_OUT.manifest > ${UNIFYFS_TEST_TMPDIR}/config_0700/stage_OUT_output.OUT 2>&1

test_expect_success "input file has been staged to output" '
    test_path_is_file ${UNIFYFS_TEST_TMPDIR}/stage_destination_0700/destination_0700.file
'

test_expect_success "final output is identical to initial input" '
    test_cmp ${UNIFYFS_TEST_TMPDIR}/stage_source/source_0700.file ${UNIFYFS_TEST_TMPDIR}/stage_destination_0700/destination_0700.file
'

test_done
