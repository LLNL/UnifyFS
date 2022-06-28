******************
Contributing Guide
******************

*First of all, thank you for taking the time to contribute!*

By using the following guidelines, you can help us make UnifyFS even better.

Getting Started
===============

Get UnifyFS
-----------

You can build and run UnifyFS by following :doc:`these instructions <build>`.

Getting Help
------------

To contact the UnifyFS team, send an email to the `mailing list`_.

---------------

Reporting Bugs
==============

Please contact us via the `mailing list`_ if you are not certain that you are
experiencing a bug.

You can open a new issue and search existing issues using the `issue tracker`_.

If you run into an issue, please search the `issue tracker`_ first to ensure
the issue hasn't been reported before. Open a new issue only if you haven't
found anything similar to your issue.

.. important::

    **When opening a new issue, please include the following information at the top of the issue:**

    - What operating system (with version) you are using
    - The UnifyFS version you are using
    - Describe the issue you are experiencing
    - Describe how to reproduce the issue
    - Include any warnings or errors
    - Apply any appropriate labels, if necessary

When a new issue is opened, it is not uncommon for developers to request
additional information.

In general, the more detail you share about a problem the more quickly a
developer can resolve it. For example, providing a simple test case is
extremely helpful. Be prepared to work with the developers investigating your
issue. Your assistance is crucial in providing a quick solution.

---------------

Suggesting Enhancements
=======================

Open a new issue in the `issue tracker`_ and describe your proposed feature.
Why is this feature needed? What problem does it solve? Be sure to apply the
*enhancement* label to your issue.

---------------

Pull Requests
=============

- All pull requests must be based on the current *dev* branch and apply without
  conflicts.
- Please attempt to limit pull requests to a single commit which resolves one
  specific issue.
- Make sure your commit messages are in the correct format. See the
  :ref:`Commit Message Format <commit-message-label>` section for more
  information.
- When updating a pull request, squash multiple commits by performing a
  `rebase <https://git-scm.com/docs/git-rebase>`_ (squash).
- For large pull requests, consider structuring your changes as a stack of
  logically independent patches which build on each other. This makes large
  changes easier to review and approve which speeds up the merging process.
- Try to keep pull requests simple. Simple code with comments is much easier to
  review and approve.
- Test cases should be provided when appropriate.
- If your pull request improves performance, please include some benchmark
  results.
- The pull request must pass all regression tests before being accepted.
- All proposed changes must be approved by a UnifyFS project member.

---------------

Testing
=======

All help is appreciated! If you're in a position to run the latest code,
consider helping us by reporting any functional problems, performance
regressions, or other suspected issues. By running the latest code on a wide
range of realistic workloads, configurations, and architectures we're better
able to quickly identify and resolve issues.

---------------

Documentation
=============

As UnifyFS is continually improved and updated, it is easy for documentation to
become out-of-date. Any contributions to the documentation, no matter how
small, is always greatly appreciated. If you are not in a position to update
the documentation yourself, please notify us via the `mailing list`_ of
anything you notice that is missing or needs to be changed.

---------------

***********************
Developer Documentation
***********************

Here is our current documentation of how the internals of UnifyFS function for
several basic operations.

.. rubric:: UnifyFS Developer's Documentation

.. image:: images/UnifyFS-developers-documentation.png
   :target: slides/UnifyFS-developers-documentation.pdf
   :height: 72px
   :align: left
   :alt: UnifyFS Developer's Documentation

:download:`Download PDF <slides/UnifyFS-developers-documentation.pdf>`.

|

.. explicit external hyperlink targets

.. _mailing list: ecp-unifyfs@exascaleproject.org
.. _issue tracker: https://github.com/LLNL/UnifyFS/issues
