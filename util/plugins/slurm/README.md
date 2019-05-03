### Integration with SLURM Resource Manager

To enable job-level integration of UnifyCR, where the UnifyCR daemon
is automatically launched on each node during job startup and
terminate once the job completes, a SPANK lua plugin must be added
to SLURM. Typically, these SPANK lua plugins are installed in
`/etc/slurm/lua.d` Consult with your SLURM system administrator for
more precise location information, and to make the changes described below.

We provide a lua plugin that launches and terminates a UnifyCR daemon
(i.e., `unifycrd`) on a node.

Integration of the lua plugin requires placing unifycr.lua in
`/etc/slurm/lua.`, which in most cases cannot be written to
without root access. The SLURM lua plugin supports a new "unifycr"
job allocation flag. Users may specify the allocation flag as an
option to the `srun` command:
```shell
[prompt]$ srun -N1 -n2 --unifycr <other srun options> ./jobscript
```
Alternatively, the job script may contain the flag as an `#SBATCH`
directive:
```shell
#! /bin/bash
#SBATCH --unifycr
```
