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

stage_cfg_dir=${UNIFYFS_TEST_TMPDIR}/stage/config_9300
stage_src_dir=${UNIFYFS_TEST_TMPDIR}/stage/source
stage_dst_dir=${UNIFYFS_TEST_TMPDIR}/stage/destination_9300
mkdir -p $stage_cfg_dir $stage_src_dir $stage_dst_dir

test_expect_success "stage testing dirs exist" '
    test_path_is_dir $stage_cfg_dir &&
    test_path_is_dir $stage_src_dir &&
    test_path_is_dir $stage_dst_dir
'

stage_src_file=$stage_src_dir/source_9300.file
stage_dst_file=$stage_dst_dir/destination_9300.file

rm -f $stage_cfg_dir/* $stage_dst_dir/*

test_expect_success "config_9300 directory is empty" '
    test_dir_is_empty $stage_cfg_dir
'

# NOTE: we're using the unifyfs-stage binary as its own transfer data target
# because we know it's there and it's filled with non-zero data.
stage_exe=${UNIFYFS_BUILD_DIR}/util/unifyfs-stage/src/unifyfs-stage
cp $stage_exe $stage_src_file

test_expect_success "source.file exists" '
    test_path_is_file $stage_src_file
'

stage_manifest=$stage_cfg_dir/stage.manifest
echo "\"$stage_src_file\" \"$stage_dst_file\"" > $stage_manifest

test_expect_success "config_9300 directory now has manifest file" '
    test_path_is_file $stage_manifest
'

test_expect_success "target directory is empty" '
    test_dir_is_empty $stage_dst_dir
'

stage_log=$stage_cfg_dir/stage.log
$JOB_RUN_COMMAND $stage_exe -N $stage_manifest &> $stage_log

test_expect_success "input file has been staged to output" '
    test_path_is_file $stage_dst_file
'

export TEST_CMP='cmp --quiet'

test_expect_success "final output is identical to initial input" '
    test_cmp $stage_src_file $stage_dst_file
'

test_done
