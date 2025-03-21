from IPython import embed
from pathlib import Path
from tqdm import tqdm
from typing import Dict, Tuple
import csv
import logging
import math
import multiprocessing
import pandas as pd
import time


def get_diagnosed_bugs_over_time(bugs_list, path, target):
    if target == '':
        logging.error("Error: bug_locations argument needed")
        return

    raise Exception('depreciated!')

    events = pd.read_csv(path / "events.csv")
    bugs_found_time = {}
    first_timestamp = time.time()
    last_timestamp = 0

    # construct a map from each bug to the earliest time that we've seen it
    for bug in bugs_list.keys():
        bugs_found_time[bug] = 0

    num_files = len(list(path.glob('*')))
    file_count = 0
    for f in path.iterdir():
        if f.name in ['events.csv', 'full_trace.csv', 'info.txt']:
            continue

        # read the test result file
        df = pd.read_csv(f)
        if not len(df):
            continue

        # iterate through all tests in the file
        for _, row in df.iterrows():
            # track the first and last timestamp for this test
            timestamp = row["timestamp"]
            last_timestamp = timestamp if timestamp > last_timestamp else last_timestamp
            first_timestamp = timestamp if timestamp < first_timestamp else first_timestamp

            # for each bug, iterate through the stores and see if any are the bugs we've diagnosed
            if row["ret_code"] != 0:
                diagnosed_bug = False

                for key in row.keys():
                    if key == "ret_code" or key == "message" or key == "note" or key == "timestamp":
                        continue

                    # if the store isn't present
                    # if row[key] == -1:
                    #     continue

                    # get the store with the store_id equal to the store this test reordered
                    store = events.loc[(events["store_id"] == int(key))]

                    # loop through all the files in the store and see if any of them match the diagnosed bug
                    i = 0
                    while "file_" + str(i) in store:
                        file = store.at[int(key), "file_" + str(i)]
                        line = store.at[int(key), "line_" + str(i)]
                        if math.isnan(line):
                            i += 1
                            continue

                        store_loc = str(file) + ":" + str(int(line))
                        # logging.info(store_loc)

                        for bug, bug_locs in bugs_list.items():
                            if store_loc in bug_locs:
                                diagnosed_bug = True
                                if bugs_found_time[bug] > row["timestamp"] or bugs_found_time[bug] == 0:
                                    bugs_found_time[bug] = row["timestamp"]
                                # exit looping through bug locations
                                break

                        i += 1

                    # iangneal: don't exit early, might be responsible for multiple
                    # this is because of our approximate diagnosis
                    # exit looping through stores
                    # if diagnosed_bug:
                    #     break

                if not diagnosed_bug and print_undiagnosed:
                    logging.info(f"Bug in file {f} is not in the list of diagnosed bug locations!\n")

        file_count += 1
        if file_count % 500 == 0:
            print(f"Processed {file_count}/{num_files} files")

    logging.info("Earliest timestamp for each bug:")
    logging.info(bugs_found_time)
    logging.info('\n') # print a new line

    # create a map of bugs over time
    bugs_per_timestamp = {}
    bugs_per_timestamp[first_timestamp] = 0
    bugs_per_timestamp[last_timestamp] = 0

    for bug, timestamp in bugs_found_time.items():
        if timestamp == 0:
            continue
        if timestamp not in bugs_per_timestamp.keys():
            bugs_per_timestamp[timestamp] = 1
        else:
            bugs_per_timestamp[timestamp] += 1

    # logging.info(bugs_per_timestamp)

    num_bugs = 0
    bugs_over_time = {}
    sorted_timestamps = sorted(bugs_per_timestamp.keys())

    # TODO: remove duplicate code
    for timestamp in sorted_timestamps:
        # the following line is if we're using UNIX timestamps, not relative timestamps
        # num_seconds = timestamp - first_timestamp
        num_seconds = timestamp
        num_bugs += bugs_per_timestamp[timestamp]
        bugs_over_time[num_seconds] = num_bugs

    logging.info("Bugs over time:")
    logging.info(bugs_over_time)
    return bugs_over_time


def get_diagnosed_bugs_parallelized(bugs_list, path, label, target, print_undiagnosed):
    if target == '':
        logging.error("Error: bug_locations argument needed")
        return

    events = pd.read_csv(path / "events.csv")

    file_list = list(path.glob("*"))
    # file_chunks = [file_list[i:i + CHUNK_SIZE] for i in range(0, len(file_list), CHUNK_SIZE)]

    # process_pool = []
    file_processed = 0
    num_files = len(file_list)

    # manager = multiprocessing.Manager()
    # bugs_found_time = manager.dict()
    # bugs_found_file = manager.dict()
    # min_timestamps = manager.list()
    # max_timestamps = manager.list()

    bugs_found_time = {}
    bugs_found_test = {}
    min_timestamps = []
    max_timestamps = []

    # file_args = [(bugs_list, events, f, bugs_found_time, bugs_found_file, min_timestamps, max_timestamps) for f in file_list]

    # construct a map from each bug to the earliest time that we've seen it, and corresponding test case file
    for bug in bugs_list.keys():
        bugs_found_time[bug] = None
        # bugs_found_file[bug] = 0

    first_timestamp = None
    last_timestamp = None
    all_store_times = {}
    all_store_tests = {}
    with multiprocessing.Pool() as p:
        start = time.time()
        res_it = p.imap_unordered(get_found_bugs_from_file_data, file_list)
        for i, result in enumerate(res_it):
            if "store_time" not in result:
                continue
            store_times = result["store_time"]
            test_file_name = result["file_name"]
            etime = result['earliest_timestamp']
            ltime = result['latest_timestamp']
            if first_timestamp is None or (etime is not None and etime < first_timestamp):
                first_timestamp = etime
            if last_timestamp is None or (ltime is not None and ltime > last_timestamp):
                last_timestamp = ltime

            for store_id, timestamp in store_times.items():
                if store_id in all_store_times:
                    all_store_times[store_id] = min(all_store_times[store_id], timestamp)
                    all_store_tests[store_id] = test_file_name if all_store_times[store_id] == timestamp else all_store_tests[store_id]
                else:
                    all_store_times[store_id] = timestamp
                    all_store_tests[store_id] = test_file_name

            print(f'Completed {i+1}/{len(file_list)}...', end='\r')

    print(f'Completed {len(file_list)} file parses in {time.time() - start} seconds.')
    print(f'{len(all_store_times) = }')

    start = time.time()
    for store_id, timestamp in all_store_times.items():
        # get the store with the store_id equal to the store this test reordered
        store = events.loc[(events["store_id"] == store_id)].loc[store_id]
        diagnosed_bug = False

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
                    if  bugs_found_time[bug] is None or bugs_found_time[bug] > timestamp:
                        bugs_found_time[bug] = timestamp
                        bugs_found_test[bug] = all_store_tests[store_id]
                    # exit looping through bug locations
                    break

            i += 1

        if not diagnosed_bug and print_undiagnosed:
            logging.info(f"Bug from store {store_id = } is not in the list of diagnosed bug locations!\n")

    print(f'Completed {len(all_store_times)} diagnoses in {time.time() - start} seconds.')


    logging.info(f"Earliest timestamp for each bug in {label} testing:")
    logging.info(bugs_found_time)
    logging.info('\n') # print a new line
    logging.info(f"Test case file for each bug in {label} testing:")
    logging.info(bugs_found_test)
    logging.info('\n') # print a new line

    logging.info(f"First timestamp in {label} testing on {target} benchmark: {first_timestamp}")
    logging.info(f"Last timestamp in {label} testing on {target} benchmark: {last_timestamp}")

    # create a map of bugs over time
    bugs_per_timestamp = {}
    bugs_per_timestamp[first_timestamp] = 0
    bugs_per_timestamp[last_timestamp] = 0

    for bug, timestamp in bugs_found_time.items():
        if timestamp == 0:
            continue
        if timestamp not in bugs_per_timestamp.keys():
            bugs_per_timestamp[timestamp] = 1
        else:
            bugs_per_timestamp[timestamp] += 1

    # logging.info(bugs_per_timestamp)

    num_bugs = 0
    bugs_over_time = {}
    sorted_timestamps = sorted([ k for k in bugs_per_timestamp.keys() if k is not None ])

    # TODO: remove duplicate code
    for timestamp in sorted_timestamps:
        num_seconds = timestamp
        num_bugs += bugs_per_timestamp[timestamp]
        bugs_over_time[num_seconds] = num_bugs

    logging.info(f"Bugs over time in {label} testing on {target} benchmark:")
    logging.info(bugs_over_time)
    logging.info(f'Removing found bugs from target')
    for bug, timefound in bugs_found_time.items():
        if timefound is not None:
            bugs_list.pop(bug)
    logging.info(f'Remaining bugs for this target {bugs_list.keys()}')
    return bugs_over_time


dict_lock = multiprocessing.Lock()
ts_lock = multiprocessing.Lock()
def process_file(args):
    bugs_list, events, f, bugs_found_time, bugs_found_file, min_timestamps, max_timestamps = args
    if f.name in ['events.csv', 'full_trace.csv', 'info.txt']:
        return

    first_timestamp = time.time()
    last_timestamp = 0

    # read the test result file
    df = pd.read_csv(f)
    if not len(df):
        return

    # iterate through all tests in the file
    for _, row in df.iterrows():
        # track the first and last timestamp for this test
        timestamp = row["timestamp"]
        last_timestamp = timestamp if timestamp > last_timestamp else last_timestamp
        first_timestamp = timestamp if timestamp < first_timestamp else first_timestamp

        # for each bug, iterate through the stores and see if any are the bugs we've diagnosed
        if row["ret_code"] == 0:
            continue

        diagnosed_bug = False

        for key in row.keys():
            if key == "ret_code" or key == "message" or key == "note" or key == "timestamp":
                continue

            # if the store isn't present
            # if row[key] == -1:
            #     continue

            # get the store with the store_id equal to the store this test reordered
            store = events.loc[(events["store_id"] == int(key))]

            # loop through all the files in the store and see if any of them match the diagnosed bug
            i = 0
            while "file_" + str(i) in store:
                file = store.at[int(key), "file_" + str(i)]
                line = store.at[int(key), "line_" + str(i)]
                if math.isnan(line):
                    i += 1
                    continue

                store_loc = str(file) + ":" + str(int(line))
                # logging.info(store_loc)

                for bug, bug_locs in bugs_list.items():
                    if store_loc in bug_locs:
                        diagnosed_bug = True
                        with dict_lock:
                            if bugs_found_time[bug] > row["timestamp"] or bugs_found_time[bug] == 0:
                                bugs_found_time[bug] = row["timestamp"]
                                bugs_found_file[bug] = str(f.resolve())
                        # exit looping through bug locations
                        break

                i += 1

            # iangneal: don't exit early, a row may be responsible for more than one bug
            # this is because of our approximate diagnosis
            # exit looping through stores
            # if diagnosed_bug:
            #     break

        if not diagnosed_bug and print_undiagnosed:
            logging.info(f"Bug in file {f} is not in the list of diagnosed bug locations!\n")

    with ts_lock:
        min_timestamps.append(first_timestamp)
        max_timestamps.append(last_timestamp)

def process_file_native(args):
    bugs_list, events, f, bugs_found_time, bugs_found_file, min_timestamps, max_timestamps = args
    if f.name in ['events.csv', 'full_trace.csv', 'info.txt']:
        return

    f = f.resolve()
    fstr = str(f)

    first_timestamp = time.time()
    last_timestamp = 0

    # read the test result file
    with f.open() as fp:
        reader = csv.reader(fp)
        test_store_ids = None
        rows = []
        for entries in reader:
            if test_store_ids is None:
                test_store_ids = [ int(e) for e in entries[:-4] ]
                continue
            # print(f'{entries = }')
            ret_code = int(entries[-4])
            if ret_code == 0:
                continue

            timestamp = int(entries[-1])
            store_ids = [ test_store_ids[i] for i, s in enumerate(entries[:-4]) if int(s) >= 0 ]
            if not store_ids:
                continue

            # print(entries)
            # print(store_ids)
            row = [store_ids, ret_code, timestamp]
            # print(row)
            # exit(1)

            rows += [row]

    if not rows:
        return

    # iterate through all tests in the file
    for store_ids, ret_code, timestamp in rows:
        # track the first and last timestamp for this test
        last_timestamp = timestamp if timestamp > last_timestamp else last_timestamp
        first_timestamp = timestamp if timestamp < first_timestamp else first_timestamp

        diagnosed_bug = False

        for store_id in store_ids:

            # if the store isn't present
            # if row[key] == -1:
            #     continue

            # get the store with the store_id equal to the store this test reordered
            store = events.loc[(events["store_id"] == store_id)]

            # loop through all the files in the store and see if any of them match the diagnosed bug
            i = 0
            while "file_" + str(i) in store:
                file = store.at[store_id, "file_" + str(i)]
                line = store.at[store_id, "line_" + str(i)]
                if math.isnan(line):
                    i += 1
                    continue

                store_loc = str(file) + ":" + str(int(line))
                # logging.info(store_loc)

                for bug, bug_locs in bugs_list.items():
                    if store_loc in bug_locs:
                        diagnosed_bug = True
                        with dict_lock:
                            if bugs_found_time[bug] > timestamp or bugs_found_time[bug] == 0:
                                bugs_found_time[bug] = timestamp
                                bugs_found_file[bug] = fstr
                        # exit looping through bug locations
                        break

                i += 1

            # iangneal: don't exit early, a row may be responsible for more than one bug
            # this is because of our approximate diagnosis
            # exit looping through stores
            # if diagnosed_bug:
            #     break

        if not diagnosed_bug and print_undiagnosed:
            logging.info(f"Bug in file {f} is not in the list of diagnosed bug locations!\n")

    with ts_lock:
        min_timestamps.append(first_timestamp)
        max_timestamps.append(last_timestamp)

def process_file_chunks(bugs_list, events, file_chunk, bugs_found_time, bugs_found_file, min_timestamps, max_timestamps):
    for f in file_chunk:
        process_file(bugs_list, events, f, bugs_found_time, bugs_found_file, min_timestamps, max_timestamps)

def get_found_bugs_from_file_data(f: Path):
    '''
    What I really need is, store and first appearance of the store (don't need file)
    '''
    excluded_files = ['events.csv', 'full_trace.csv', 'info.txt', 'groups.csv', 'testing_completed']
    if f.name in excluded_files:
        return {}

    earliest = None
    latest = None
    # read the test result file
    with f.open() as fp:
        # avoid possible null bytes
        reader = csv.reader( (line.replace('\0','') for line in fp) )
        test_store_ids = None
        store_time = {}
        messages = {}
        notes = {}
        ret_codes = {}
        for entries in reader:
            if test_store_ids is None:
                test_store_ids = [ int(e) for e in entries[:-4] ]
                continue

            timestamp = int(entries[-1])

            if earliest is None or earliest > timestamp:
                earliest = timestamp
            if latest is None or latest < timestamp:
                latest = timestamp

            # print(f'{entries = }')
            ret_code = int(entries[-4])
            if ret_code == 0:
                continue

            message = entries[-3]
            note = entries[-2]

            # iangneal: some of the store locations mean "if this store happens, there is a bug"
            # and some mean "if this store is missing, there is a bug". So let's overgeneralize.
            # store_ids = [ test_store_ids[i] for i, s in enumerate(entries[:-4]) if int(s) >= 0 ]
            store_ids = [ test_store_ids[i] for i, _ in enumerate(entries[:-4]) ]
            # if not store_ids:
            #     # This means that the previous store is responsible.
            #     previous_store_id = test_store_ids[0] - 1
            #     if previous_store_id < 0:
            #         continue
            #     store_ids = [previous_store_id]
            previous_store_id = test_store_ids[0] - 1
            if previous_store_id >= 0:
                store_ids += [previous_store_id]

            for store_id in store_ids:
                if store_id in store_time:
                    store_time[store_id] = min(store_time[store_id], timestamp)
                else:
                    store_time[store_id] = timestamp
                messages[store_id] = message
                notes[store_id] = note
                ret_codes[store_id] = ret_code

        result = {
            'store_time': store_time,
            'file_name': f.resolve(),
            'earliest_timestamp': earliest,
            'latest_timestamp': latest,
            'message': messages,
            'note': notes,
            'ret_code': ret_codes,
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