#!/bin/bash
#
# Test unifyfs-stage executable for basic functionality
#

test_description="Test basic functionality of unifyfs-stage executable"

. $(dirname $0)/sharness.sh

test_expect_success "unifyfs-stage exists" '
    test_path_is_file ${UNIFYFS_BUILD_DIR}/util/unifyfs-stage/src/unifyfs-stage
'
test_expect_success "testing temp dir exists" '
    test_path_is_dir  ${UNIFYFS_TEST_TMPDIR}
'

stage_cfg_dir=${UNIFYFS_TEST_TMPDIR}/stage/config_0700
stage_src_dir=${UNIFYFS_TEST_TMPDIR}/stage/source
stage_dst_dir=${UNIFYFS_TEST_TMPDIR}/stage/destination_0700
mkdir -p $stage_cfg_dir $stage_src_dir $stage_dst_dir

test_expect_success "stage testing dirs exist" '
    test_path_is_dir $stage_cfg_dir &&
    test_path_is_dir $stage_src_dir &&
    test_path_is_dir $stage_dst_dir
'

stage_src_file=$stage_src_dir/source_0700.file
stage_im_file=$UNIFYFS_TEST_MOUNT/intermediate_0700.file
stage_dst_file=$stage_dst_dir/destination_0700.file

dd if=/dev/urandom bs=4M count=1 of=$stage_src_file &>/dev/null

test_expect_success "source.file exists" '
    test_path_is_file $stage_src_file
'

rm -f $stage_cfg_dir/* $stage_dst_dir/*

test_expect_success "config_0700 directory is empty" '
    test_dir_is_empty $stage_cfg_dir
'

stage_in_manifest=$stage_cfg_dir/stage_IN.manifest
stage_out_manifest=$stage_cfg_dir/stage_OUT.manifest

echo "\"$stage_src_file\" \"$stage_im_file\"" > $stage_in_manifest
echo "\"$stage_im_file\" \"$stage_dst_file\"" > $stage_out_manifest

test_expect_success "config_0700 directory now has manifest files" '
    test_path_is_file $stage_in_manifest &&
    test_path_is_file $stage_out_manifest
'

test_expect_success "target directory is empty" '
    test_dir_is_empty $stage_dst_dir
'

stage_in_log=$stage_cfg_dir/stage_IN.log
stage_out_log=$stage_cfg_dir/stage_OUT.log
stage_exe=${UNIFYFS_BUILD_DIR}/util/unifyfs-stage/src/unifyfs-stage

$JOB_RUN_COMMAND $stage_exe -v -m ${UNIFYFS_TEST_MOUNT} -S $stage_cfg_dir $stage_in_manifest &> $stage_in_log

$JOB_RUN_COMMAND $stage_exe -v -m ${UNIFYFS_TEST_MOUNT} -S $stage_cfg_dir $stage_out_manifest &> $stage_out_log

test_expect_success "input file has been staged to output" '
    test_path_is_file $stage_dst_file
'

export TEST_CMP='cmp --quiet'

test_expect_success "final output is identical to initial input" '
    test_might_fail test_cmp $stage_src_file $stage_dst_file
'

test_done
