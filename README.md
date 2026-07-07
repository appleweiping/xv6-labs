# xv6-labs — MIT 6.S081 Operating System Engineering (2021)

> A complete, from-skeleton set of solutions to all ten labs of
> **6.S081 / 6.1810 — Operating System Engineering** (MIT), built on the
> `xv6-riscv` teaching kernel, as part of a
> [csdiy.wiki](https://csdiy.wiki/) full-catalog build.

![status](https://img.shields.io/badge/status-complete-brightgreen)
![labs](https://img.shields.io/badge/labs-10%2F10%20passing-brightgreen)
![language](https://img.shields.io/badge/C-informational)
![arch](https://img.shields.io/badge/RISC--V-qemu-blue)
![license](https://img.shields.io/badge/license-MIT-blue)

## Overview

xv6 is a re-implementation of Unix V6 for RISC-V, used by MIT's 6.S081 to teach
operating-system internals. This repository implements every one of the course's
ten labs — system calls, page tables, traps, copy-on-write fork, user-level
threads, a network device driver, fine-grained kernel locking, an on-disk file
system, and memory-mapped files — each fully passing the course's own autograder.

Each lab lives on its **own git branch** (mirroring the upstream 6.S081 layout,
where every lab is a separate branch that builds on the base kernel). `main`
holds the unmodified official skeleton; check out a lab branch to see that lab's
implementation and its captured grader output under `results/`.

## Results (measured on WSL2 Ubuntu, QEMU `qemu-system-riscv64`, TCG emulation)

Every lab was graded with the course's own `grade-lab-<name>` script and scores
its maximum. Because the guest is RISC-V emulated on an x86 host (no KVM for a
cross-architecture guest), the long CPU-bound tests (`usertests`, `bigfile`) are
slow; they are run with a `GRADE_TIMEOUT_SCALE` environment variable that scales
every per-test wall-clock timeout **without changing what is tested** (default
`1` = identical to the upstream MIT grader). Every individual test reports `OK`.

| Lab | Branch | What it implements | Grader score |
|---|---|---|---|
| 1. Utilities        | `util`    | `sleep`, `pingpong`, `primes`, `find`, `xargs` user programs | **100/100** |
| 2. System calls     | `syscall` | `trace` syscall + `sysinfo` syscall | **35/35** |
| 3. Page tables      | `pgtbl`   | per-process USYSCALL page, `pgaccess`, PTE printer | **46/46** |
| 4. Traps            | `traps`   | RISC-V backtrace + `sigalarm`/`sigreturn` | **85/85** |
| 5. Copy-on-write    | `cow`     | lazy copy-on-write `fork` with page refcounts | **110/110** |
| 6. Multithreading   | `thread`  | user-level threads, `ph` locking, `barrier` | **60/60** |
| 7. Networking       | `net`     | e1000 NIC driver (`transmit`/`recv` rings) | **100/100** |
| 8. Locks            | `lock`    | per-CPU kmem freelists + bucket-hashed buffer cache | **70/70** |
| 9. File system      | `fs`      | doubly-indirect blocks (big files) + symbolic links | **100/100** |
| 10. mmap            | `mmap`    | lazy memory-mapped files (`mmap`/`munmap`) | **140/140** |

Full captured autograder transcripts are on each branch under
`results/grade-lab-<name>.txt`.

## Per-branch layout

```
main      # unmodified official xv6-riscv (xv6-labs-2021) skeleton
util      # main + Lab 1
syscall   # main + Lab 2
pgtbl     # main + Lab 3
traps     # main + Lab 4
cow       # main + Lab 5
thread    # main + Lab 6
net       # main + Lab 7
lock      # main + Lab 8
fs        # main + Lab 9
mmap      # main + Lab 10
```

Each lab branch = the base skeleton (`main`) + a "chore: import <lab> skeleton"
commit + a "feat(<lab>): …" implementation commit + a "test(<lab>): …" commit
recording the real grader score.

## How to run

Requires a RISC-V cross toolchain and QEMU. On Ubuntu / WSL2:

```bash
sudo apt-get install -y build-essential gdb-multiarch qemu-system-misc \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu python3

# Check out a lab and boot xv6 (conf/lab.mk selects the lab via LAB=<name>):
git checkout mmap
make qemu           # boots xv6; run 'mmaptest', then 'usertests', etc. Ctrl-a x to quit

# Run the lab's autograder:
python3 ./grade-lab-mmap
# On slow (emulated) hosts, give the CPU-bound tests more time — this only
# scales timeouts, it does not change the tests:
GRADE_TIMEOUT_SCALE=6 python3 ./grade-lab-mmap
```

Note: on the WSL2 `/mnt` (9p) mount, mass-file test suites can stall; grading was
done by copying the tree to a native ext4 path (`rsync`/`git archive` to `~`),
running the grader there, and keeping git operations on the mounted repo.

## Highlights of what each lab required

- **pgtbl** — a read-only page (`USYSCALL`) shared with user space for a
  syscall-free `getpid`, plus `pgaccess` reading the PTE accessed bits.
- **cow** — deferring page copies in `fork` until a write faults, with a
  per-physical-page reference count so pages are freed exactly once.
- **thread** — a cooperative user threading library (context save/restore in
  `uthread_switch.S`) and correct `pthread_mutex`/condition use in `ph`/`barrier`.
- **net** — driving the QEMU e1000: filling TX descriptors and advancing the
  tail, and draining the RX ring into `net_rx()`, correctly handling the
  descriptor-done status bits.
- **lock** — removing lock contention by giving each CPU its own `kalloc`
  freelist (with stealing) and hashing the buffer cache into per-bucket locks.
- **fs** — a doubly-indirect block pointer to raise the max file size to 65803
  blocks, and `symlink` with cycle-bounded resolution in `open`.
- **mmap** — VMAs per process, lazy page-fault population from the backing file,
  write-back of dirty `MAP_SHARED` pages on `munmap`, and VMA copy across `fork`.

## Tech stack

C (kernel and user space), RISC-V assembly, GNU Make, a RISC-V cross toolchain
(`gcc-riscv64-linux-gnu`), and `qemu-system-riscv64`. Graders are Python 3.

## Credits & license

Based on the labs of **MIT 6.S081 / 6.1810 — Operating System Engineering**, which
use the `xv6-riscv` teaching operating system by Frans Kaashoek, Robert Morris,
Russ Cox and the MIT PDOS group. This repository is an independent educational
implementation of the lab exercises; the xv6 kernel, lab specifications, and test
harnesses belong to their original authors. xv6 is distributed under the MIT
License (see [LICENSE](LICENSE)); the lab solutions added here are released under
the same terms.
