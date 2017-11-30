================
Interception
================

POSIX calls can be intercepted via three methods.
   
---------------------------
Dynamically
---------------------------
        1. `GOTCHA <https://github.com/LLNL/GOTCHA>`_ add description 

        2. LD_PRELOAD add description

---------------------------
Statically
---------------------------
        3. --wrap add description 
 
---------------------------
How To Use Each Method 
---------------------------

        GOTCHA
        - This is the default and just requires linking with unifycr in your 
        - application (-lunifycr)

        LD_PRELOAD    

        WRAP    
        - This is not the default and requires rebuilding the unifycr library 
        - with a -DUNIFYCR_PRELOAD, then adding the following flags to your application: 
        - add flag example
                
