#!/usr/bin/bash

# get first argument as folder
result_dir=$1

# get all files name in subgraph_*.dot
files=$(ls $result_dir/subgraph_*.dot)

# iterate over all files
for file in $files
do
    # get file name without extension
    file_name=$(basename $file .dot)
    # generate png file
    dot -Tpng $file -o $result_dir/$file_name.png
done