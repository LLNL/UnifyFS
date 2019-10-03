### Integration with IBM Platform LSF with Cluster System Manager (CSM)

To enable job-level integration of UnifyFS, where the UnifyFS daemon
is automatically launched on each node during job startup and
terminate once the job completes, additions must be made
to the CSM prologue and epilogue Python scripts. Typically,
these CSM scripts are found somewhere within the `/opt/ibm/csm`
directory and named `privileged_prolog` and `privileged_epilog`.
Consult with your LSF system administrator for more precise location
information, and to make the changes described below.

We provide helper scripts that launch and terminate a UnifyFS daemon
(i.e., `unifyfsd`) on a node. The scripts are:
* launch: `<INSTALL_PREFIX>/sbin/unifyfs_lsfcsm_prolog`
* terminate: `<INSTALL_PREFIX>/sbin/unifyfs_lsfcsm_epilog`

Integration of the helper scripts requires editing the CSM prologue and
epilogue scripts to support a new "unifyfs" job allocation flag.
Users may specify the allocation flag as an option to the `bsub`
command:
```shell
[prompt]$ bsub -alloc_flags unifyfs <other bsub options> jobscript.lsf
```
Alternatively, the job script may contain the flag as a `#BSUB`
directive:
```shell
#! /bin/bash -l
#BSUB -alloc_flags UNIFYFS
```

Supporting the new flag is as simple as adding a new `elif` block to
the flag parsing loop, as shown below for an excerpt from
`privileged_prolog`. Note that the CSM scripts use uppercase strings
for flag comparison.

```python
flags = args.user_flags.split(" ")
regex = re.compile ("(\D+)(\d*)$")
for feature in flags:
    fparse = regex.match (feature.upper())
    if hasattr(fparse, 'group'):
        if fparse.group(0) == "HUGEPAGES":
            os.chmod("/dev/hugepages", 1777)
        elif fparse.group(0) == "UNIFYFS":
            cmd = ['%s/unifyfs_lsfcsm_prolog' % JOB_SCRIPT_DIR]
            runcmd (cmd)
```

The provided helper scripts should be installed as root to the
`JOB_SCRIPT_DIR` used by the CSM scripts (e.g., `/opt/ibm/csm/prologs`).


