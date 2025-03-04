# Bug 0
## Description
Missing fdatasync on MANIFEST file, rename can be reordered before fdatasync

## Setup

Run `setup.sh` to compile LevelDB and the workload.

## Pathfinder Config
pathfinder/targets/leveldb-bug-0/pathfinder-config.ini

## Repo
https://github.com/google/leveldb, Commit: 8cce47e450b365347769959c53b8836ef0216df9

