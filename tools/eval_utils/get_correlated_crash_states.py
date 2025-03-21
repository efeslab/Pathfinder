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

def group_by_root_cause_parallelized(bugs_list, path):

    events = pd.read_csv(path / "events.csv")
    
    full_trace = pd.read_csv(path / "full_trace.csv")
    # only takes "timestamp", "store_id", and "event_type" columns
    full_trace = full_trace[["timestamp", "store_id", "event_type"]]

    file_list = list(path.glob("*"))


    # bug id to crash state count with non-zero ret code
    bug_id_to_hit_crash_state = {}
    bug_id_to_hit_file = {}
    # bug id to crash state count with zero ret code
    bug_id_to_miss_crash_state = {}
    bug_id_to_miss_file = {}
    
    #     # derive which stores corresponds to each bug
    
    # bug_to_affected_stores = {}
    # for bug_id, bug_locs in bugs_list.items():
    #     bug_to_affected_stores[bug_id] = []
    #     for count, store_id in enumerate(events["store_id"].unique()):
    #         store = events.loc[(events["store_id"] == store_id)].loc[store_id]
    #         if count % 10000 == 0:
    #             print(f'Completed {count} stores... on bug {bug_id}', end='\r')
    #         i = 0
    #         while f'file_{i}' in store:
    #             file = store.at[f'file_{i}']
    #             line = store.at[f'line_{i}']
    #             if math.isnan(line):
    #                 i += 1
    #                 continue
    #             store_loc = f'{file}:{int(line)}'
    #             if store_loc in bug_locs:
    #                 bug_to_affected_stores[bug_id].append(store_id)
    #             i += 1
    
    # Use ThreadPoolExecutor for parallel processing
    func = partial(get_found_bugs_from_file_data, full_store_events=events, full_trace=full_trace, bugs_list=bugs_list)                
        
    # with concurrent.futures.ThreadPoolExecutor(max_workers=NUM_PROCESSES) as executor:
    #     start = time.time()
    #     # Submit all the tasks and get a list of futures
    #     futures = {executor.submit(func, file): file for file in file_list}

    #     # Process the results as they complete
    #     i = 0
    #     for future in concurrent.futures.as_completed(futures):
    #         file = futures[future]
    #         try:
    #             result = future.result()
    #             # Do something with the result
    #             if result == {} or result is None:
    #                 continue
    #             ret_codes_by_crash_state = result['ret_codes_by_crash_state']
    #             bugs_found_by_crash_state = result['bugs_found_by_crash_state']
    #             for crash_state_id, ret_code in ret_codes_by_crash_state.items():
    #                 if ret_code == 0:
    #                     # print(bugs_found_by_crash_state)
    #                     for bug_id in bugs_found_by_crash_state[crash_state_id]:
    #                         if bug_id not in bug_id_to_miss_crash_state:
    #                             bug_id_to_miss_crash_state[bug_id] = 0
    #                             bug_id_to_miss_file[bug_id] = []
    #                         bug_id_to_miss_crash_state[bug_id] += 1
    #                         bug_id_to_miss_file[bug_id].append(result['file_name'])
    #                 else:
    #                     for bug_id in bugs_found_by_crash_state[crash_state_id]:
    #                         if bug_id not in bug_id_to_hit_crash_state:
    #                             bug_id_to_hit_crash_state[bug_id] = 0
    #                             bug_id_to_hit_file[bug_id] = []
    #                         bug_id_to_hit_crash_state[bug_id] += 1
    #                         bug_id_to_hit_file[bug_id].append(result['file_name'])
    #         except Exception as e:
    #             print(f"File {file} generated an exception: {e}")
            
    #         print(f'Completed {i+1}/{len(file_list)}...', end='\r')
    #         i += 1
    with multiprocessing.Pool(processes=NUM_PROCESSES) as p:
        start = time.time()

        res_it = p.imap_unordered(func, file_list)
        for i, result in enumerate(res_it):
            if result == {} or result is None:
                continue
            ret_codes_by_crash_state = result['ret_codes_by_crash_state']
            bugs_found_by_crash_state = result['bugs_found_by_crash_state']
            for crash_state_id, ret_code in ret_codes_by_crash_state.items():
                if ret_code == 0:
                    # print(bugs_found_by_crash_state)
                    for bug_id in bugs_found_by_crash_state[crash_state_id]:
                        if bug_id not in bug_id_to_miss_crash_state:
                            bug_id_to_miss_crash_state[bug_id] = 0
                            bug_id_to_miss_file[bug_id] = []
                        bug_id_to_miss_crash_state[bug_id] += 1
                        bug_id_to_miss_file[bug_id].append(result['file_name'])
                else:
                    for bug_id in bugs_found_by_crash_state[crash_state_id]:
                        if bug_id not in bug_id_to_hit_crash_state:
                            bug_id_to_hit_crash_state[bug_id] = 0
                            bug_id_to_hit_file[bug_id] = []
                        bug_id_to_hit_crash_state[bug_id] += 1
                        bug_id_to_hit_file[bug_id].append(result['file_name'])
                        
            print(f'Completed {i+1}/{len(file_list)}...', end='\r')

    print(f'Completed {len(file_list)} file parses in {time.time() - start} seconds.')
    # print(f'{len(all_store_times) = }')
    print(f'Bug id to hit crash state count: {bug_id_to_hit_crash_state}')
    print(f'Bug id to miss crash state count: {bug_id_to_miss_crash_state}')
    return bug_id_to_hit_crash_state, bug_id_to_hit_file, bug_id_to_miss_crash_state, bug_id_to_miss_file


def get_bugs_found_by_crash_state(store_ids, bugs_list, events, full_trace):
    bug_ids_found = []
    
    for store_id in store_ids:
        # get the store with the store_id equal to the store this test reordered
        store = events.loc[(events["store_id"] == store_id)].loc[store_id]
        
        
        # loop through all the files in the store and see if any of them match the diagnosed bug
        i = 0
        while f'file_{i}' in store:
            file = store.at[f'file_{i}']
            line = store.at[f'line_{i}']
            if math.isnan(line):
                i += 1
                continue

            store_loc = f'{file}:{int(line)}'

            for bug, bug_locs in bugs_list.items():
                if store_loc in bug_locs:
                    bug_ids_found.append(bug)
            i += 1
    return bug_ids_found

def get_found_bugs_from_file_data(f: Path, full_store_events: pd.DataFrame, full_trace: pd.DataFrame, bugs_list: Dict[str, list]):
    '''
    
    For root cause grouping, for each crash state tested, output the bug or list of bugs it touches
    '''
    excluded_files = ['events.csv', 'full_trace.csv', 'info.txt', 'groups.csv', 'testing_completed']
    if f.name in excluded_files:
        return {}

    # read the test result file
    with f.open() as fp:
        # avoid possible null bytes
        reader = csv.reader( (line.replace('\0','') for line in fp) )
        test_store_ids = None
        store_time = {}
        messages = {}
        notes = {}
        ret_codes = {}
        # crash_state id : ret_code
        ret_codes_by_crash_state = {}
        # crash_state id : bug_ids
        bugs_found_by_crash_state = {}
        crash_state_id = 0
        for entries in reader:
            bugs_found_by_crash_state[crash_state_id] = []
            if test_store_ids is None:
                test_store_ids = [ int(e) for e in entries[:-4] ]
                # assert test_store_ids increases by 1
                # try:
                #     assert (all(test_store_ids[i] == test_store_ids[i-1] + 1 for i in range(1, len(test_store_ids))))                
                # except:
                #     print(f'{test_store_ids = }')
                #     print(f'{entries = }')
                #     print(f'{f = }')
                #     assert False
                
                # Yile: for accurate bug crash state accounting
                
                # get the event ids for from the min store id to max store id
                # print(full_trace.loc[(full_trace["store_id"] == test_store_ids[0])])
                min_event_id = full_trace.loc[(full_trace["store_id"] == test_store_ids[0])].iloc[0]['timestamp']
                max_event_id = full_trace.loc[(full_trace["store_id"] == test_store_ids[-1])].iloc[0]['timestamp']
                events = full_trace.loc[(full_trace["timestamp"] >= min_event_id) & (full_trace["timestamp"] <= max_event_id)]
                # print(f'events: {events["timestamp"]}')
                
                # now split the events by into list of events, split by event_type == FENCE
                fence_indices = events.loc[events["event_type"] == "FENCE"].index
                fence_start = events.loc[events["timestamp"] == min_event_id].index[0]
                fence_end = events.loc[events["timestamp"] == max_event_id].index[0]
                fence_indices = fence_indices.insert(0, fence_start)
                fence_indices = fence_indices.insert(len(fence_indices), fence_end)
                # print(f'{fence_indices = }')
                events_list = [events.loc[(events["timestamp"] >= fence_indices[i]) & (events["timestamp"] <= fence_indices[i+1]) & (events["event_type"] == "STORE")]["store_id"].to_list() for i in range(len(fence_indices)-1)]
                # print(f'{len(events_list) = } :{events_list}')
                # return {}
                
                    # derive which stores corresponds to each bug
                
                bug_to_affected_stores = {}
                for bug_id, bug_locs in bugs_list.items():
                    bug_to_affected_stores[bug_id] = []
                    for count, store_id in enumerate(test_store_ids):
                        store = full_store_events.loc[(full_store_events["store_id"] == store_id)].loc[store_id]
                        # if count % 10000 == 0:
                        #     print(f'Completed {count} stores... on bug {bug_id}', end='\r')
                        i = 0
                        while f'file_{i}' in store:
                            file = store.at[f'file_{i}']
                            line = store.at[f'line_{i}']
                            if math.isnan(line):
                                i += 1
                                continue
                            store_loc = f'{file}:{int(line)}'
                            if store_loc in bug_locs:
                                bug_to_affected_stores[bug_id].append(store_id)
                            i += 1
                # if bug_to_affected_stores['0'] != []:
                #     print(f"{bug_to_affected_stores['0']}")
                bug_to_affected_ranges = {}
                for bug_id, affected_stores in bug_to_affected_stores.items():
                    affected_ranges = []
                    if not affected_stores:
                        continue
                    for store_id in affected_stores:
                        if store_id not in test_store_ids:
                            continue
                        for i, event_list in enumerate(events_list):
                            if store_id in event_list and i not in affected_ranges:
                                affected_ranges.append(i)
                    bug_to_affected_ranges[bug_id] = affected_ranges
                continue


            # print(f'{entries = }')
            ret_code = int(entries[-4])
            ret_codes_by_crash_state[crash_state_id] = ret_code


            # iangneal: some of the store locations mean "if this store happens, there is a bug"
            # and some mean "if this store is missing, there is a bug". So let's overgeneralize.
            # store_ids = [ test_store_ids[i] for i, s in enumerate(entries[:-4]) if int(s) >= 0 ]
            applied_store_ids = [ test_store_ids[i] for i, s in enumerate(entries[:-4]) if int(s) >= 0 ]
            
            for bug_id, affected_ranges in bug_to_affected_ranges.items():
                cur_range_idx = 0
                for store_range in events_list:
                    if all(store_id in applied_store_ids for store_id in store_range):
                        cur_range_idx += 1
                    else:
                        break
                # if all are applied, then add the last range
                if cur_range_idx == len(events_list):
                    cur_range_idx -= 1
                if cur_range_idx in affected_ranges:
                    bugs_found_by_crash_state[crash_state_id].append(bug_id)
                # if non of the stores in this current range is applied, then also add previous range
                non_applied = True
                for store in events_list[cur_range_idx]:
                    if store in applied_store_ids:
                        non_applied = False
                        break 
                if non_applied:
                    if cur_range_idx != 0:
                        if cur_range_idx - 1 in affected_ranges:
                            bugs_found_by_crash_state[crash_state_id].append(bug_id)

            crash_state_id += 1
            
        result = {
            'file_name': f.resolve(),
            'ret_codes_by_crash_state': ret_codes_by_crash_state,
            'bugs_found_by_crash_state': bugs_found_by_crash_state
        }
        return result

def get_stores(path: Path) -> pd.DataFrame:
    return pd.read_csv(path / "events.csv")

def find_bugs_in_results_parallelized(bugs_list, path) -> Tuple[Dict, object]:
    '''
    Returns diagnosed, undiagnosed
    '''
    events = pd.read_csv(path / "events.csv")

    file_list = list(path.glob("*"))

    bugs_found_time = {}

    undiagnosed_bugs = []

    # construct a map from each bug to the earliest time that we've seen it, and corresponding test case file
    for bug in bugs_list.keys():
        bugs_found_time[bug] = None

    first_timestamp = None
    last_timestamp = None
    all_store_times = {}
    all_store_info = {}
    with multiprocessing.Pool() as p:
        res_it = enumerate(
            p.imap_unordered(get_found_bugs_from_file_data, file_list))
        for i, result in tqdm(res_it, total=len(file_list),
                                desc='Locating bugs...'):
            if 'store_time' not in result:
                continue

            store_times = result['store_time']
            etime = result['earliest_timestamp']
            ltime = result['latest_timestamp']
            if first_timestamp is None \
                    or (etime is not None and etime < first_timestamp):
                first_timestamp = etime
            if last_timestamp is None \
                    or (ltime is not None and ltime > last_timestamp):
                last_timestamp = ltime

            for store_id, timestamp in store_times.items():
                if store_id in all_store_times:
                    all_store_times[store_id] = min(all_store_times[store_id], timestamp)
                    all_store_info[store_id] = result if all_store_times[store_id] == timestamp else all_store_info[store_id]
                else:
                    all_store_times[store_id] = timestamp
                    all_store_info[store_id] = result

    for store_id, timestamp in tqdm(all_store_times.items(),
                                    total=len(all_store_times),
                                    desc='Diagnosing bugs...'):
        # get the store with the store_id equal to the store this test reordered
        store = events.loc[(events["store_id"] == store_id)].loc[store_id]
        diagnosed_bug = False

        if math.isnan(store['line_0']):
            # Nothing to go on for this, so skip it.
            continue

        # loop through all the files in the store and see if any of them match the diagnosed bug
        i = 0
        while f'file_{i}' in store:
            file = store.at[f'file_{i}']
            line = store.at[f'line_{i}']
            if math.isnan(line):
                i += 1
                continue

            store_loc = f'{file}:{int(line)}'

            for bug, bug_locs in bugs_list.items():
                if store_loc in bug_locs:
                    diagnosed_bug = True
                    if bugs_found_time[bug] is None or bugs_found_time[bug] > timestamp:
                        bugs_found_time[bug] = first_timestamp
                    # exit looping through bug locations
                    break

            i += 1

        if not diagnosed_bug:
            undiagnosed_bugs += [
                (store,
                 pd.DataFrame(all_store_info[store_id]))
            ]

    return bugs_found_time, undiagnosed_bugs



def get_max_timestamp_from_file(f: Path):

    excluded_files = ['events.csv', 'full_trace.csv', 'info.txt', 'groups.csv', 'testing_completed']
    if f.name in excluded_files:
        return -1

    latest = -1
    # read the test result file
    with f.open() as fp:
        # avoid possible null bytes
        reader = csv.reader( (line.replace('\0','') for line in fp) )

        for entries in reader:
            if entries[-1] == 'timestamp':
                continue
            timestamp = int(entries[-1])
            latest = max(latest, timestamp)

        return latest


def get_latest_timestamp(path) -> int:
    '''
    Returns latest testing timestamp
    '''

    file_list = list(path.glob("*"))

    with multiprocessing.Pool() as p:
        return max(p.imap_unordered(get_max_timestamp_from_file, file_list))
    
if __name__ == '__main__':
    # create a logging file
    logging.basicConfig(filename='get_correlated_crash_states.log', level=logging.DEBUG)
    all_bug_locs = bug_locations.get_bugs()
    targets = {
        # "memcached": ["/home/yilegu/squint/justification/data/storage/squint/results/release/memcached/memcached_res_rep"],
        # "level_hashing": ["/home/yilegu/squint/justification/data/storage/squint/results/release/level_hashing/level_hashing_res_exhaust"]
        # "hse_v2": ["/home/yilegu/squint/justification/data/storage/squint/results/release/hse/hse_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/hse/hse_res_rep"],
        # "hse_v1": ["/home/yilegu/squint/justification/data/storage/squint/results/release/hse_rollback/hse_res_rollback_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/hse_rollback/hse_res_rollback_rep"],
        # "redis": ["/home/yilegu/squint/justification/data/storage/squint/results/release/redis/redis_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/redis/redis_res_rep"],
        # "level_hashing": ["/home/yilegu/squint/justification/data/storage/squint/results/release/level_hashing/level_hashing_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/level_hashing/level_hashing_res_rep"],
        # "memcached": ["/home/yilegu/squint/justification/data/storage/squint/results/release/memcached/memcached_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/memcached/memcached_res_rep"],
        # "pmdk_array": ["/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_array_v1.4/pmdk_array_v1.4_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_array_v1.4/pmdk_array_v1.4_res_rep"],
        # "fast_fair": ["/home/yilegu/squint/justification/data/storage/squint/results/release/FAST_FAIR/FAST_FAIR_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/FAST_FAIR/FAST_FAIR_res_rep"],
        # "pmdk_btree": ["/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_btree_v1.4/pmdk_btree_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_btree_v1.4/pmdk_btree_res_rep"],
        # "pmdk_hashmap_atomic": ["/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_hashmap_atomic_v1.4/pmdk_hashmap_atomic_v1.4_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_hashmap_atomic_v1.4/pmdk_hashmap_atomic_v1.4_res_rep"],
        # "pmdk_rbtree": ["/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_rbtree_v1.4/pmdk_rbtree_v1.4_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/pmdk_rbtree_v1.4/pmdk_rbtree_v1.4_res_rep"],
        # "cceh": ["/home/yilegu/squint/justification/data/storage/squint/results/release/cceh/cceh_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/cceh/cceh_res_rep"],
        # "woart": ["/home/yilegu/squint/justification/data/storage/squint/results/release/woart/woart_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/woart/woart_res_rep"],
        # "wort": ["/home/yilegu/squint/justification/data/storage/squint/results/release/wort/wort_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/wort/wort_res_rep"],
        # "p_art": ["/home/yilegu/squint/justification/data/storage/squint/results/release/P_ART/P_ART_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_ART/P_ART_res_rep"],
        # "p_bwtree": ["/home/yilegu/squint/justification/data/storage/squint/results/release/P_BwTree/P_BwTree_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_BwTree/P_BwTree_res_rep"],
        # "p_clht": ["/home/yilegu/squint/justification/data/storage/squint/results/release/P_CLHT/P_CLHT_Witcher_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_CLHT/P_CLHT_Witcher_res_rep", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_CLHT_Witcher_Aga/P_CLHT_Witcher_Aga_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_CLHT_Witcher_Aga/P_CLHT_Witcher_Aga_res_rep", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_CLHT_Witcher_Aga_TX/P_CLHT_Witcher_Aga_TX_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_CLHT_Witcher_Aga_TX/P_CLHT_Witcher_Aga_TX_res_rep"],
        # "p_hot": ["/home/yilegu/squint/justification/data/storage/squint/results/release/P_HOT/P_HOT_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_HOT/P_HOT_res_rep"],
        # "p_masstree": ["/home/yilegu/squint/justification/data/storage/squint/results/release/P_MassTree/P_MassTree_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/P_MassTree/P_MassTree_res_rep"],
        
        "hse_v2": ["/home/yilegu/squint/justification/data/storage/squint/results/release/hse/hse_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/hse/hse_res_rep"],
        "hse_v1": ["/home/yilegu/squint/justification/data/storage/squint/results/release/hse_rollback/hse_res_rollback_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/hse_rollback/hse_res_rollback_rep"],
        "redis": ["/home/yilegu/squint/justification/data/storage/squint/results/release/redis/redis_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/redis/redis_res_rep"],
        "memcached": ["/home/yilegu/squint/justification/data/storage/squint/results/release/memcached/memcached_res_exhaust", "/home/yilegu/squint/justification/data/storage/squint/results/release/memcached/memcached_res_rep"],
        
        
        
    }
    for target, paths in targets.items():
        for path in paths:
            bugs_list = all_bug_locs[target]
            path = Path(path)
            bug_id_to_hit_crash_state, bug_id_to_hit_file, bug_id_to_miss_crash_state, bug_id_to_miss_file = group_by_root_cause_parallelized(bugs_list, path)
            logging.info(f'path: {path}')
            logging.info(f'bug_id_to_hit_crash_state: {bug_id_to_hit_crash_state}')
            # logging.info(f'bug_id_to_hit_file: {bug_id_to_hit_file}')
            logging.info(f'bug_id_to_miss_crash_state: {bug_id_to_miss_crash_state}')
            # logging.info(f'bug_id_to_miss_file: {bug_id_to_miss_file}')