# Bug-0

A crash during startup from backup can lose metadata

https://jira.mongodb.org/browse/WT-9926?jql=project%20%3D%20%2212782%22%20AND%20issuetype%20%3D%20Bug%20AND%20text%20~%20fsync%20ORDER%20BY%20created%20DESC%2C%20timespent%20DESC%2C%20cf%5B10855%5D%20ASC
https://source.wiredtiger.com/10.0.0/build-posix.html#posix_building
https://github.com/wiredtiger/wiredtiger/commit/b32ec77d65b3e5e8ce2d3acf4918a8df014c8e69
./configure && make

```
conda activate wiredtiger-bug-1-before
cd wiredtiger
sh autogen.sh
make distclean
./configure --enable-python  --enable-snappy --enable-lz4 --enable-zlib --enable-zstd --with-python_prefix=$CONDA_PREFIX CFLAGS="-g"
make -j
cd lang/python
python3 setup_pip.py sdist
pip install dist/wiredtiger-10.0.2.tar.gz

export LD_LIBRARY_PATH=/home/yilegu/squint/bug_study/wiredtiger-1-recover-from-backup/wiredtiger-before/.libs:$LD_LIBRARY_PATH

export LD_LIBRARY_PATH=/home/yilegu/squint/bug_study/wiredtiger-1-recover-from-backup/wiredtiger-after/.libs:$LD_LIBRARY_PATH
```

## Code
git@github.com:efeslab/wiredtiger.git
Branch: origin/jiexiao