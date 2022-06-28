=========================================
VerifyIO: Determine UnifyFS Compatibility
=========================================

----------------------
Recorder and  VerifyIO
----------------------

VerifyIO_ can be used to determine an application's compatibility with UnifyFS
as well as aid in narrowing down what an application may need to change to
become compatible with UnifyFS.

VerifyIO is a tool within the Recorder_ tracing framework that takes the
application traces from Recorder and determines whether I/O synchronization is
correct based on the underlying file system semantics (e.g., POSIX, commit) and
synchronization semantics (e.g., POSIX, MPI).

Run VerifyIO with commit semantics on the application's traces to determine
compatibility with UnifyFS.

----------

--------------
VerifyIO Guide
--------------

To use VerifyIO, the Recorder library needs to be installed. See the `Recorder
README`_ for full instructions on how to build, run, and use Recorder.

Build
*****

Clone the ``pilgrim`` (default) branch of Recorder:

.. code-block:: Bash
    :caption: Clone

    $ git clone https://github.com/uiuc-hpc/Recorder.git

Determine the install locations of the MPI-IO and HDF5 libraries being used by
the application and pass those paths to Recorder at configure time.

.. code-block:: Bash
    :caption: Configure, Make, and Install

    $ deps_prefix="${mpi_install};${hdf5_install}"
    $ mkdir -p build install

    $ cd build
    $ cmake -DCMAKE_INSTALL_PREFIX=../install -DCMAKE_PREFIX_PATH=$deps_prefix ../Recorder
    $ make
    $ make install

    # Capture Recorder source code and install locations
    $ export RECORDER_SRC=/path/to/Recorder/source/code
    $ export RECORDER_ROOT=/path/to/Recorder/install

Python3 and the ``recorder-viz`` and ``networkx`` packages are also required to
run the final VerifyIO verification code.

.. code-block:: Bash
    :caption: Install Python Packages

    $ module load python/3.x.x
    $
    $ pip3 install recorder-viz --user
    $ pip3 install networkx --user

Run
***

Before capturing application traces, it is recommended to disable data sieving
as VerifyIO will flag this as incompatible under commit semantics.

.. code-block:: Bash
    :caption: Disable Data Sieving

    echo -e "romio_ds_write disable\nromio_ds_read disable" > /path/to/romio_hints
    export ROMIO_HINTS=/path/to/romio_hints
    export ROMIO_PRINT_HINTS=1   #optional


Run the application with Recorder to capture the traces using the appropriate
environment variable export option for the available workload manager.

.. code-block:: Bash
    :caption: Capture Traces

    srun -N $nnodes -n $nprocs --export=ALL,LD_PRELOAD=$RECORDER_ROOT/lib/librecorder.so example_app_executable

Recorder places the trace files in a folder within the current working directory
named ``hostname-username-appname-pid-starttime``.

.. _recorder2text-label:
If desired (e.g., for debugging), use the recorder2text tool to generate
human-readable traces from the captured trace files.

.. code-block:: Bash
    :caption: Generate Human-readable Traces

    $RECORDER_ROOT/bin/recorder2text /path/to/traces &> recorder2text.out

This will generate text-format traces in the folder ``path/to/traces/_text``.

Next, run the Recorder conflict detector to capture **potential** conflicts. The
``--semantics=`` option needs to match the semantics of the intended underlying
file system. In the case of UnifyFS, use ``commit`` semantics.

.. code-block:: Bash
    :caption: Capture Potential Conflicts

    $RECORDER_ROOT/bin/conflict_detector /path/to/traces --semantics=commit &> conflict_detector_commit.out

The potential conflicts will be recorded to the file
``path/to/traces/conflicts.txt``.

Lastly, run VerifyIO with the traces and potential conflicts to determine
whether all I/O operations are properly synchronized under the desired standard
(e.g., POSIX, MPI).

.. code-block:: Bash
    :caption: Run VerifyIO

    # Evaluate using POSIX standard
    python3 $RECORDER_SRC/tools/verifyio/verifyio.py /path/to/traces /path/to/traces/conflicts.txt --semantics=posix &> verifyio_commit_results.posix

    # Evaluate using MPI standard
    python3 $RECORDER_SRC/tools/verifyio/verifyio.py /path/to/traces /path/to/traces/conflicts.txt --semantics=mpi &> verifyio_commit_results.mpi

Interpreting Results
********************

In the event VerifyIO shows an incompatibility, or the results are not clear,
don't hesitate to contact the UnifyFS team `mailing list`_ for aid in
determining a solution.

Conflict Detector Results
^^^^^^^^^^^^^^^^^^^^^^^^^

When there are no potential conflicts, the conflict detector output simply
states as much:

.. code-block:: none

    [prompt]$ cat conflict_detector_commit.out
    Check potential conflicts under Commit Semantics
    ...
    No potential conflict found for file /path/to/example_app_outfile

When potential conflicts exist, the conflict detector prints a list of each
conflicting pair. For each operation within a pair, the output contains the
process rank, sequence ID, offset the conflict occurred at, number of bytes
affected by the operation, and whether the operation was a write or a read.
This format is printed at the top of the output.

.. code-block:: none

    [prompt]$ cat conflict_detector_commit.out
    Check potential conflicts under Commit Semantics
    Format:
    Filename, io op1(rank-seqId, offset, bytes, isRead), io op2(rank-seqId, offset, bytes, isRead)

    /path/to/example_app_outfile, op1(0-244, 0, 800, write), op2(0-255, 0, 96, write)
    /path/to/example_app_outfile, op1(0-92, 4288, 2240, write), op2(0-148, 4288, 2216, read)
    /path/to/example_app_outfile, op1(1-80, 6528, 2240, write), op2(1-136, 6528, 2216, read)
    ...
    /path/to/example_app_outfile, op1(0-169, 18480, 4888, write), op2(3-245, 18848, 14792, read)
    /path/to/example_app_outfile, op1(0-169, 18480, 4888, write), op2(3-246, 18848, 14792, write)
    /path/to/example_app_outfile, op1(0-231, 18480, 16816, write), op2(3-245, 18848, 14792, read)
    /path/to/example_app_outfile, Read-after-write (RAW): D-2,S-5, Write-after-write (WAW): D-1,S-2

The final line printed contains a summary of all the potential conflicts.
This consists of the total number of read-after-write (RAW) and
write-after-write (WAW) potentially conflicting operations performed by
different processes (D-#) or the same process (S-#).

VerifyIO Results
^^^^^^^^^^^^^^^^

VerifyIO takes the traces and potential conflicts and checks if each conflicting pair is properly synchronized. Refer to the `VerifyIO README <VerifyIO>`_ for a
description on what determines proper synchronization for a conflicting I/O
pair.

Compatible with UnifyFS
"""""""""""""""""""""""

In the event that there are no potential conflicts, or each potential conflict
pair was performed by the same rank, VerifyIO will report the application as
being properly synchronized and therefore compatible with UnifyFS.

.. code-block:: none

    [prompt]$ cat verifyio_commit_results.posix
    Rank: 0, intercepted calls: 79, accessed files: 5
    Rank: 1, intercepted calls: 56, accessed files: 2
    Building happens-before graph
    Nodes: 46, Edges: 84

    Properly synchronized under posix semantics


    [prompt]$ cat verifyio_commit_results.mpi
    Rank: 0, intercepted calls: 79, accessed files: 5
    Rank: 1, intercepted calls: 56, accessed files: 2
    Building happens-before graph
    Nodes: 46, Edges: 56

    Properly synchronized under mpi semantics

When there are potential conflicts from different ranks but the proper
synchronization has occurred, VerifyIO will also report the application as being
properly synchronized.

.. code-block:: none

    [prompt]$ cat verifyio_commit_results.posix
    Rank: 0, intercepted calls: 510, accessed files: 8
    Rank: 1, intercepted calls: 482, accessed files: 5
    Rank: 2, intercepted calls: 481, accessed files: 5
    Rank: 3, intercepted calls: 506, accessed files: 5
    Building happens-before graph
    Nodes: 299, Edges: 685
    Conflicting I/O operations: 0-169-write <--> 3-245-read, properly synchronized: True
    Conflicting I/O operations: 0-169-write <--> 3-246-write, properly synchronized: True

    Properly synchronized under posix semantics

Incompatible with UnifyFS
"""""""""""""""""""""""""

In the event there are potential conflicts from different ranks but the proper
synchronization has **not** occurred, VerifyIO will report the application as
not being properly synchronized and therefore incompatible [*]_ with UnifyFS.

Each operation involved in the conflicting pair is listed in the format
``rank-sequenceID-operation`` followed by the whether that pair is properly
synchronized.

.. code-block:: none

    [prompt]$ cat verifyio_commit_results.mpi
    Rank: 0, intercepted calls: 510, accessed files: 8
    Rank: 1, intercepted calls: 482, accessed files: 5
    Rank: 2, intercepted calls: 481, accessed files: 5
    Rank: 3, intercepted calls: 506, accessed files: 5
    Building happens-before graph
    Nodes: 299, Edges: 427
    0-169-write --> 3-245-read, properly synchronized: False
    0-169-write --> 3-246-write, properly synchronized: False

    Not properly synchronized under mpi semantics

.. [*] Incompatible here does not mean the application cannot work with UnifyFS
   at all, just under the default configuration. There are
   :doc:`workarounds <limitations>` available that could very easily change this
   result (VerifyIO plans to have options to run under the assumption some
   workarounds are in place). Should your outcome be an incompatible result,
   please contact the UnifyFS `mailing list`_ for aid in finding a solution.

.. rubric:: Debugging a Conflict

The :ref:`recorder2text output <recorder2text-label>` can be used to aid in
narrowing down where/what is causing a conflicting pair. In the incompatible
example above, the first pair is a ``write()`` from rank 0 with the sequence ID
of 169 and a ``read()`` from rank 3 with the sequence ID of 245.

The sequence IDs correspond to the order in which functions were called by that
particular rank. In the recorder2text output, this ID will then correspond to
line numbers, but off by +1 (i.e., seqID 169 -> line# 170).

.. code-block:: none
    :caption: recorder2text output
    :emphasize-lines: 6,14

        #rank 0
        ...
    167 0.1440291 0.1441011 MPI_File_write_at_all 1 1 ( 0-0 0 %p 1 MPI_TYPE_UNKNOWN [0_0] )
    168 0.1440560 0.1440679 fcntl 2 0 ( /path/to/example_app_outfile 7 1 )
    169 0.1440700 0.1440750 pread 2 0 ( /path/to/example_app_outfile %p 4888 18480 )
    170 0.1440778 0.1440909 pwrite 2 0 ( /path/to/example_app_outfile %p 4888 18480 )
    171 0.1440918 0.1440987 fcntl 2 0 ( /path/to/example_app_outfile 6 2 )
        ...

        #rank 3
        ...
    244 0.1539204 0.1627174 MPI_File_write_at_all 1 1 ( 0-0 0 %p 1 MPI_TYPE_UNKNOWN [0_0] )
    245 0.1539554 0.1549513 fcntl 2 0 ( /path/to/example_app_outfile 7 1 )
    246 0.1549534 0.1609544 pread 2 0 ( /path/to/example_app_outfile %p 14792 18848 )
    247 0.1609572 0.1627053 pwrite 2 0 (/path/to/example_app_outfile %p 14792 18848 )
    248 0.1627081 0.1627152 fcntl 2 0 ( /path/to/example_app_outfile 6 2 )
        ...

Note that in this example the ``pread()``/``pwrite()`` calls from rank 3 operate
on overlapping bytes from the ``pwrite()`` call from rank 0. For this example,
data sieving was left enabled which results in "fcntl-pread-pwrite-fcntl" I/O
sequences. Refer to :doc:`limitations` for more on the file locking limitations
of UnifyFS.

The format of the recorder2text output is: ``<start-time> <end-time>
<func-name> <call-level> <func-type> (func-parameters)``

.. Note::

    The ``<call-level>`` value indicates whether the function was called
    directly by the application or by an I/O library. The ``<func-type>`` value
    shows the Recorder-tracked function type.

    +-------+------------------------------------+-+-------+-----------------+
    | Value | Call Level                         | | Value | Function Type   |
    +=======+====================================+=+=======+=================+
    | 0     | Called by application directly     | | 0     | RECORDER_POSIX  |
    +-------+------------------------------------+ +-------+-----------------+
    | 1     | - Called by HDF5                   | | 1     | RECORDER_MPIIO  |
    |       | - Called by MPI (no HDF5)          | +-------+-----------------+
    |       |                                    | | 2     | RECORDER_MPI    |
    +-------+------------------------------------+ +-------+-----------------+
    | 2     | Called by MPI, which was called by | | 3     | RECORDER_HDF5   |
    |       | HDF5                               | +-------+-----------------+
    |       |                                    | | 4     | RECORDER_FTRACE |
    +-------+------------------------------------+-+-------+-----------------+

.. explicit external hyperlink targets

.. _mailing list: ecp-unifyfs@exascaleproject.org
.. _Recorder: https://github.com/uiuc-hpc/Recorder
.. _Recorder README: https://github.com/uiuc-hpc/Recorder/blob/pilgrim/README.md
.. _VerifyIO: https://github.com/uiuc-hpc/Recorder/tree/pilgrim/tools/verifyio#note-on-the-third-step
