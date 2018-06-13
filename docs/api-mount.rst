=================
Mounting UnifyCR
=================

In this section, we describe how to use the UnifyCR API in an application.

---------------------------
Mounting 
---------------------------

To use the UnifyCR filesystem a user will have to provide a path prefix. All 
file operations under the path prefix will be intercepted by the UnifyCR 
filesystem. For instance, to use UnifyCR on all path prefixes that begin with 
/tmp this would require a:

.. code-block:: C

        unifycr_mount('/tmp', rank, rank_num, 0);

Where /tmp is the path prefix you want UnifyCR to intercept. The rank and rank 
number is the rank you are currently on, and the number of tasks you have 
running in your job. Lastly, the zero corresponds to the app id.

---------------------------
Unmounting 
---------------------------

When you are finished using UnifyCR in your application, you should unmount. 
  
.. code-block:: C

        if (rank == 0) {
            unifycr_unmount();
        }

It is only necessary to call unmount once on rank zero.
