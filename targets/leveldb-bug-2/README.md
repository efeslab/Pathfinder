# Bug 2
## Description
Unlink of MANIFEST can be reordered before Rename of tmp to CURRENT on non-ext4 filesystem

## Squint Config
squint/targets/leveldb-bug-2/squint-config.ini
Need to disable decompose_syscall for this bug to manifest.

## Repo
Commit: 607c20951808c204d8abe33fe6d7d958467dd19f, Github Repo and Branch: https://github.com/efeslab/leveldb/commits/leveldb-bug-2

