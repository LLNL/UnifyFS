*************
Wrapper Guide
*************

.. warning::

    This document is out-of-date as the process for generating
    :ref:`unifycr_list.txt <unifycr-list-label>` has bugs which causes the
    generation of :ref:`gotcha_map_unifycr_list.h <gotcha-list-label>` to have
    bugs as well. More information on this can be found in `issue #172
    <https://github.com/LLNL/UnifyCR/issues/172>`_.

    An updated guide and scripts needs to be created for writing and adding
    new wrappers to UnifyCR.


The files in `client/check_fns/
<https://github.com/LLNL/UnifyCR/tree/dev/client/check_fns>`_ folder help
manage the set of wrappers that are implemented. In particular, they are used
to enable a tool that detects I/O routines used by an application that are not
yet supported in UnifyCR. They are also used to generate the code required for
GOTCHA.

- fakechroot_list.txt_ - lists I/O routines from fakechroot
- gnulibc_list.txt_ - I/O routines from libc
- cstdio_list.txt_ - I/O routines from stdio
- posix_list.txt_ - I/O routines in POSIX
- unifycr_list.txt_ - list of wrappers in UnifyCR
- unifycr_unsupported_list.txt_ - list of wrappers in UnifyCR that are
  implemented, but not supported

------------

unifycr_check_fns Tool
======================

This tool identifies the set of I/O calls used in an application by running nm
on the executable. It reports any I/O routines used by the app, which are not
supported by UnifyCR. If an application uses an I/O routine that is not
supported, it likely cannot use UnifyCR. If the tool does not report
unsupported wrappers, the app may work with UnifyCR but it is not guaranteed to
work.

.. code-block:: Bash

    unifycr_check_fns <executable>

------------

.. _gotcha-list-label:

Building the GOTCHA List
========================

The gotcha_map_unifycr_list.h_ file contains the code necessary to wrap I/O
functions with GOTCHA. This is generated from the
:ref:`unifycr_list.txt <unifycr-list-label>` file by running the following
command:

.. code-block:: Bash

    python unifycr_translate.py unifycr_list

------------

Commands to Build Files
=======================

fakechroot_list.txt
-------------------

The fakechroot_list.txt_ file lists I/O routines implemented in fakechroot.
This list was generated using the following commands:

.. code-block:: Bash

    git clone https://github.com/fakechroot/fakechroot.git fakechroot.git
    cd fakechroot.git/src
    ls *.c > fakechroot_list.txt

gnulibc_list.txt
----------------

The gnulibc_list.txt_ file lists I/O routines available in libc. This list was
written by hand using information from
http://www.gnu.org/software/libc/manual/html_node/I_002fO-Overview.html#I_002fO-Overview.

cstdio_list.txt
---------------

The cstdio_list.txt_ file lists I/O routines available in libstdio. This list
was written by hand using information from
http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf.

.. _unifycr-list-label:

unifycr_list.txt
----------------

The unifycr_list.txt_ file specifies the set of wrappers in UnifyCR. Most but
not all such wrappers are supported. The command to build unifycr list:

.. code-block::

    grep UNIFYCR_WRAP ../src/\*.c > unifycr_list.txt

unifycr_unsupported_list.txt
----------------------------

The unifycr_unsupported_list.txt_ file specifies wrappers that are in UnifyCR,
but are known to not actually be supported. This list is written by hand.

.. explicit external hyperlink targets

.. _cstdio_list.txt: https://github.com/LLNL/UnifyCR/blob/dev/client/check_fns/cstdio_list.txt
.. _fakechroot_list.txt: https://github.com/LLNL/UnifyCR/blob/dev/client/check_fns/fakechroot_list.txt
.. _gotcha_map_unifycr_list.h: https://github.com/LLNL/UnifyCR/blob/dev/client/src/gotcha_map_unifycr_list.h
.. _gnulibc_list.txt: https://github.com/LLNL/UnifyCR/blob/dev/client/check_fns/gnulibc_list.txt
.. _posix_list.txt: https://github.com/LLNL/UnifyCR/blob/dev/client/check_fns/posix_list.txt
.. _unifycr_list.txt: https://github.com/LLNL/UnifyCR/blob/dev/client/check_fns/unifycr_list.txt
.. _unifycr_unsupported_list.txt: https://github.com/LLNL/UnifyCR/blob/dev/client/check_fns/unifycr_unsupported_list.txt
