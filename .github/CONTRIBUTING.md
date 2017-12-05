# Contributing to UnifyCR
<img src="https://github.com/LLNL/UnifyCR/blob/master/docs/unify-logo.png" width="100" height="85"/>

*First of all, thank you for taking the time to contribute!*

By using the following guidelines, you can help us make UnifyCR even
better.

## Table Of Contents
[What should I know before I get started?](#what-should-i-know-before-i-get-started)

  * [Get UnifyCR](#get-unifycr)
  * [Getting Help](#getting-help)

[How Can I Contribute?](#how-can-i-contribute)

  * [Reporting Bugs](#reporting-bugs)
  * [Suggesting Enhancements](#suggesting-enhancements)
  * [Pull Requests](#pull-requests)
  * [Testing](#testing)

[Style Guides](#style-guides)

  * [Coding Conventions](#coding-conventions)
  * [Commit Message Format](#commit-message-format)

## What should I know before I get started?

### Get UnifyCR
You can build and run UnifyCR by following [these
instructions](https://github.com/LLNL/UnifyCR/blob/dev/README.md).

### Getting Help
To contact the UnifyCR team, send email to the
[mailing list](mailto:ecp-unifycr@exascaleproject.org).

## How Can I Contribute?

### Reporting Bugs
Please contact us via the [mailing
list](mailto:ecp-unifycr@exascaleproject.org) if you aren't
certain that you are experiencing a bug.

If you run into an issue, please search our [issue
tracker](https://github.com/LLNL/UnifyCR/issues) first to ensure the
issue hasn't been reported before. Open a new issue only if you haven't
found anything similar to your issue.

You can open a new issue and search existing issues using the
[issue tracker](https://github.com/LLNL/UnifyCR/issues).

#### When opening a new issue, please include the following information at the top of the issue:
* What operating system (with version) you are using.
* The UnifyCR version you are using
* Describe the issue you are experiencing.
* Describe how to reproduce the issue.
* Including any warnings or errors.

When a new issue is opened, it is not uncommon for developers to request
additional information.

In general, the more detail you share about a problem the more quickly a
developer can resolve it. For example, providing a simple test case is
extremely helpful.  Be prepared to work with the developers investigating
your issue. Your assistance is crucial in providing a quick solution.

### Suggesting Enhancements

Open a new issue in the [issue tracker](https://github.com/LLNL/UnifyCR/issues) and
describe your proposed feature.  Why is this feature needed?  What problem does it solve?

### Pull Requests
* All pull requests must be based on the current *dev* branch and apply
without conflicts.
* Please attempt to limit pull requests to a single commit which resolves
one specific issue.
* Make sure your commit messages are in the correct format. See the
[Commit Message Format](#commit-message-format) section for more information.
* When updating a pull request squash multiple commits by performing a
[rebase](https://git-scm.com/docs/git-rebase) (squash).
* For large pull requests consider structuring your changes as a stack of
logically independent patches which build on each other.  This makes large
changes easier to review and approve which speeds up the merging process.
* Try to keep pull requests simple. Simple code with comments is much easier
to review and approve.
* Test cases should be provided when appropriate.
* If your pull request improves performance, please include some benchmark results.
* The pull request must pass all regression tests before
being accepted.
to control how changes are tested.
* All proposed changes must be approved by a UnifyCR project member.

### Testing
All help is appreciated! If you're in a position to run the latest code,
consider helping us by reporting any functional problems, performance
regressions or other suspected issues. By running the latest code to a wide
range of realistic workloads, configurations and architectures we're better
able to quickly identify and resolve issues.

## Style Guides

### Coding Conventions
UnifyCR follows the [Linux kernel coding
style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
except that code is indented using four spaces per level instead of tabs. Please
run `make checkstyle` to check your patch for style problems before submitting
it for review.

### Commit Message Format
Commit messages for new changes must meet the following guidelines:
* In 50 characters or less, provide a summary of the change as the
first line in the commit message.
* A body which provides a description of the change. If necessary,
please summarize important information such as why the proposed
approach was chosen or a brief description of the bug you are resolving.
Each line of the body must be 72 characters or less.

An example commit message for new changes is provided below.

```
Capitalized, short (50 chars or less) summary

More detailed explanatory text, if necessary.  Wrap it to about 72
characters or so.  In some contexts, the first line is treated as the
subject of an email and the rest of the text as the body.  The blank
line separating the summary from the body is critical (unless you omit
the body entirely); tools like rebase can get confused if you run the
two together.

Write your commit message in the imperative: "Fix bug" and not "Fixed bug"
or "Fixes bug."  This convention matches up with commit messages generated
by commands like git merge and git revert.

Further paragraphs come after blank lines.

- Bullet points are okay, too

- Typically a hyphen or asterisk is used for the bullet, followed by a
  single space, with blank lines in between, but conventions vary here

- Use a hanging indent

```
