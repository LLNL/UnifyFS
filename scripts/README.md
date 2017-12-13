## Styling code and verifying style checks

The asytle tool can be used to apply much of the required
code styling used in the project.
To apply style to a source file foo.c:

    astyle --options=scripts/unifycr.astyle foo.c

The unifycr.astyle file specifies the options used for this project.
For a full list of available asytle options,
see [http://astyle.sourceforge.net/astyle.html](http://astyle.sourceforge.net/astyle.html).

To check that uncommitted changes meet the coding style,
use the following command:

    git diff | ./scripts/checkpatch.sh
