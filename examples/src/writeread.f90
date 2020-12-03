! Copyright (c) 2020, Lawrence Livermore National Security, LLC.
! Produced at the Lawrence Livermore National Laboratory.
!
! Copyright 2020, UT-Battelle, LLC.
!
! LLNL-CODE-741539
! All rights reserved.
!
! This is the license for UnifyFS.
! For details, see https://github.com/LLNL/UnifyFS.
! Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.

program write_read_F

	implicit none

	include 'mpif.h'
	include 'unifyfsf.h'

	character*1024 :: basefname = "file"
	character*1024 :: fname, file_suffix
	character*1024 :: prefix = "/unifyfs"
	integer(kind=4) :: flag;
	integer(kind=4) :: outflags;
	integer(kind=4) :: valid;

	integer, parameter :: ni=20, nj=30, nk=45
	integer :: loop_count=5

	integer(kind=8), dimension(ni,nj,nk) :: W1, R1

	integer :: ierr, errors, all_errors, nprocs, mynod, ios
	integer :: i,j,k,loop

	integer :: writeunit, readunit
	integer(kind=8) :: nodeoff

!	integer (kind = 8) :: total_bytes_transferred
	real (kind = 8) :: total_bytes_transferred
	real (kind = 4) overall_transfer_rate
	real (kind = 8) time0, time1, iter_time0, iter_time1

	call MPI_INIT(ierr)
	call MPI_COMM_SIZE(MPI_COMM_WORLD, nprocs, ierr)
	call MPI_COMM_RANK(MPI_COMM_WORLD, mynod, ierr)

	call UNIFYFS_MOUNT(prefix, mynod, nprocs, 0, ierr)

	nodeoff=2**21

	fname = trim(prefix) // "/" // trim(basefname) // ".ckpt"

	forall(i=1:ni,j=1:nj,k=1:nk) &
	 W1(i,j,k) = nodeoff*mynod+i+ni*(j-1+nj*(k-1))

	writeunit = mynod
	open(unit=writeunit,file=fname,form='unformatted',action='write')

	write(writeunit,iostat=ios) W1
	close(writeunit)

!	R1 = 0
!	open(unit=readunit,file=fname,form='unformatted',action='read')
!	read(readunit,iostat=ios) R1
!	close(readunit)

	call UNIFYFS_UNMOUNT(ierr)
	call MPI_FINALIZE(ierr)

	end program write_read_F
