from pathlib import Path
from tqdm import tqdm
from typing import Dict, Tuple
import csv
import logging
import math
import multiprocessing
import pandas as pd
import time
from functools import partial
import concurrent.futures

import bug_locations

# NUM_PROCESSES = min(multiprocessing.cpu_count(), 256)
NUM_PROCESSES = 40
# NUM_PROCESSES = 1

def get_num_crash_states_parallelized(path):

    file_list = list(path.glob("*"))
    
    # Use ThreadPoolExecutor for parallel processing
    func = partial(get_num_crash_states)                
    
    total_crash_states = 0
        
    with multiprocessing.Pool(processes=NUM_PROCESSES) as p:
        start = time.time()

        res_it = p.imap_unordered(func, file_list)
        for i, result in enumerate(res_it):
            total_crash_states += result
            print(f'Completed {i+1}/{len(file_list)}...', end='\r')

    print(f'Completed {len(file_list)} file parses in {time.time() - start} seconds.')
    # print(f'{len(all_store_times) = }')
    return total_crash_states

def get_num_crash_states(f: Path):
    '''
    
    Get number of unique crash states in a test result file
    '''
    excluded_files = ['events.csv', 'full_trace.csv', 'info.txt', 'groups.csv', 'testing_completed']
    if f.name in excluded_files:
        return 0

    crash_state_count = 0
    # read the test result file
    with f.open() as fp:
        # avoid possible null bytes
        reader = csv.reader( (line.replace('\0','') for line in fp) )
        
            
        # for entries in reader:
        #     print(f"entry {entries}")
        #     if entries[-1] == 'timestamp':
        #         continue
        #     crash_state_count += 1
        crash_state_count = len(list(reader)) - 1
        # print(f'{f.name = }, {crash_state_count = }')
    return crash_state_count

    
if __name__ == '__main__':
    # create a logging file
    logging.basicConfig(filename='get_total_crash_states.log', level=logging.DEBUG)
    # get_num_crash_states(Path("/home/yilegu/squint/justification/data/storage/squint/results/release/hse_rollback/hse_res_rollback_rep/2_0.csv"))
    # all_bug_locs = bug_locations.get_bugs()
    targets = {
        "hse_v2": ["/home/yilegu/squint/justification/data/storage/squint/results/release/hse/hse_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/hse/hse_res_rep"],
        "hse_v1": ["/home/yilegu/squint/justification/data/storage/squint/results/release/hse_rollback/hse_res_rollback_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/hse_rollback/hse_res_rollback_rep"],
        "redis": ["/home/yilegu/squint/justification/data/storage/squint/results/release/redis/redis_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/redis/redis_res_rep"],
        "memcached": ["/home/yilegu/squint/justification/data/storage/squint/results/release/memcached/memcached_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/memcached/memcached_res_rep"],
    }
    for target, paths in targets.items():
        for path in paths:
            path = Path(path)
            total_crash_states = get_num_crash_states_parallelized(path)
            logging.info(f'path: {path}')
            logging.info(f'total_crash_states: {total_crash_states}')