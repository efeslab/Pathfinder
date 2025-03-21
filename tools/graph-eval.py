#! /usr/bin/env python3

from IPython import embed
from argparse import ArgumentParser, Namespace
from collections import defaultdict
from multiprocessing import Pool
from pathlib import Path
from tqdm import tqdm
from typing import List
import json
import logging
import math
import matplotlib.pyplot as plt
import matplotlib.ticker as tkr
import numpy as np
import os
import pandas as pd
import shlex
import shutil
import subprocess
import sys

pd.set_option('display.max_rows', None)

UTILS_PATH = Path('/home/yilegu/squint/pm-cc-bug-finder/tools') / 'eval_utils'
if not UTILS_PATH.exists():
    raise Exception(
        f'Cannot find {UTILS_PATH}, which is required for running the eval!')

sys.path.append(str(UTILS_PATH))
from bug_locations import *
from eval_targets import *
from locate_bugs_in_results import *
from graph_configs import *

SQUINT_EXE_PATH = (Path('/home/yilegu/squint/pm-cc-bug-finder/build') / 'squint/squint-pm').resolve()

TARGETS_PATH = (Path('/home/yilegu/squint/pm-cc-bug-finder/build') / 'targets').resolve()

RESULTS_PATH = (Path('/home/yilegu/squint/pm-cc-bug-finder/tools/bug_graphs/bug_vs_time/mmio')).resolve()

def plot_line(config, max_mins):
    x = [ t[0] for t in config['data'] ]
    y = [ t[1] for t in config['data'] ]

    # print(x, y)

    # exit()

    if 'linestyle' not in config:
        config['linestyle'] = 'solid'

    plt.step(x, y,
             where='post', label=config['label'],
             color=config['color'], linestyle=config['linestyle'])
    if x[-1] / 60 < max_mins:
        plt.scatter(x[-1], y[-1],
                    marker='x', color=config['color'],
                    linestyle='solid')
    else:
        last_y = 0
        for x, y in zip(x, y):
            if x <= 60 * max_mins - 7: # make the marker look nicer
                last_y = y
        plt.scatter(60 * max_mins - 7, last_y,
                    marker='x', color=config['color'], linestyle='solid')

def _finalize_graph(args: Namespace, max_time: float, max_bugs: float) -> None:
    # Set graph parameters and create the graph:
    TICKSIZE = 8
    LEGENDSIZE = 8
    LABELSIZE = 8
    TITLESIZE = 8
    N_YTICKS = 6
    N_XTICKS = 6
    # add a slight gap on the left of the graph proportional to the size of the graph
    # plt.xlim(-max_time, max_time)

    ystep = max(int(math.ceil(max_bugs / N_YTICKS)), 1)
    maxytick = max_bugs + ystep
    plt.yticks(np.arange(0, maxytick, step=ystep), fontsize=TICKSIZE)

    xstep = int(math.ceil(max_time / N_XTICKS))
    maxxtick = max_time + xstep
    plt.xticks(np.arange(0, maxxtick, step=xstep), fontsize=TICKSIZE)
    plt.xlim(0, 120)
    plt.ylabel("Number of Bugs", fontsize=LABELSIZE)
    plt.xlabel("Time (Minutes)", fontsize=LABELSIZE)
    plt.legend(loc='upper right', prop={'size':LEGENDSIZE})
    if args.title is not None and args.title:
        plt.title(args.title, fontsize=TITLESIZE)

    fig = plt.gcf()
    fig.set_size_inches(3.5, 2.0)
    fig.tight_layout()

    plt.savefig(args.output, dpi=300, bbox_inches='tight', pad_inches=0.02)
    plt.close()

def _finalize_args(args: Namespace) -> None:
    if args.output is None:
        i = 0
        while args.output is None or args.output.exists():
            args.output = Path(f'{args.target}_{i:03d}.pdf')
            i += 1
    args.output = args.output.absolute()

def _get_summary(path: Path) -> dict:
    assert path.exists()
    with path.open() as f:
        return json.load(f)

def _get_bugs_over_time(summary_data, last_timestamp):
    '''
    Maps time -> current total bugs found

    Also translates time in seconds to minutes.
    '''
    time_counts = defaultdict(int)
    for _, bug_time in summary_data.items():
        for _, seconds in bug_time.items():
            time_counts[seconds] += 1

    # Now make it cumulative
    current_total = 0
    cumulative_count = [ (0, 0) ]
    for ts in sorted(time_counts.keys()):
        time_count = time_counts[ts]
        current_total += time_count
        cumulative_count += [ (ts / 60, current_total) ]

    cumulative_count += [ (last_timestamp / 60, current_total) ]

    return cumulative_count

def main() -> None:
    parser = ArgumentParser()
    parser.add_argument('target', type=str, choices=GROUP_NAMES)
    parser.add_argument('--squint-rep-summary', type=Path,
                        default=(RESULTS_PATH / 'full/summary.json'))
    parser.add_argument('--alice-summary', type=Path,
                        default=(RESULTS_PATH / 'full_selective/summary.json'))
    parser.add_argument('--output', '-o', type=Path,
                        help='Output graph file name.')
    parser.add_argument('--title', '-t', type=str)

    args = parser.parse_args()

    _finalize_args(args)

    summary_data = {}
    if args.squint_rep_summary is not None and args.squint_rep_summary.exists():
        summary_data[SQUINT_REP_LABEL] = _get_summary(args.squint_rep_summary)
    if args.alice_summary is not None and args.alice_summary.exists():
        summary_data[SQUINT_SELECTIVE_LABEL] = _get_summary(args.alice_summary)

    assert len(summary_data) > 0, 'No summary data.'
    print(summary_data)
    eval_system_data_points = {}

    max_time = -1
    max_bugs = -1

    for eval_system, summary in summary_data.items():
        assert args.target in TARGET_GROUPS
        target_data = { k: v['total'] for k, v in summary.items()
                       if k in TARGET_GROUPS[args.target] }
        last_timestamp = max([ v['last_timestamp'] for k, v in summary.items()
                       if k in TARGET_GROUPS[args.target] ])
        data_points = _get_bugs_over_time(target_data, last_timestamp)
        eval_system_data_points[eval_system] = data_points
        max_time = max(max_time, data_points[-1][0])
        max_bugs = max(max_bugs, data_points[-1][1])


    for eval_system, ts_data in eval_system_data_points.items():
        line_fn = LINE_FNS[eval_system]
        config = line_fn(ts_data)

        plot_line(config, max_time)

    _finalize_graph(args, max_time, max_bugs)

    print(f'Plotted graph in file {str(args.output)}')

if __name__ == '__main__':
    main()
