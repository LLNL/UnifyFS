# Contents of contrib

This file describes the contents of this directory.

## Prepare developer environment

The buildme scripts can be used by developers to install
the correct version of autotools, the dependent libraries,
astyle, and build unifycr itself.

This provides a consistent environment for all developers.

Commands to set up a developer environment for UnifyCR.
Requires that gcc v4.9.3+ is in your path:

    cd contrib
    ./buildme_autotools.sh
    ./buildme_dependencies.sh
    ./buildme_astyle.sh
    ./buildme.sh

## Styling code and verifying style checks

The asytle tool can be used to apply much of the required
code styling used in the project.
To apply style to a source file foo.c:

    contrib/astyle/src/bin/astyle --options=contrib/unifycr.astyle foo.c

Available astyle options are documented at [http://astyle.sourceforge.net/astyle.html](http://astyle.sourceforge.net/astyle.html).

To check that uncommitted changes meet the coding style,
use the following command:

    git diff | ./scripts/checkpatch.sh
