#!/usr/bin/env python3
'''Utilities for analysis of Metamac log files.
Author: Nathan Flick
'''

import csv
from math import *
import re

import numpy as np
import matplotlib.pyplot as plt

def try_parse_value(s):
    try:
        return int(s)
    except ValueError:
        try:
            return float(s)
        except ValueError:
            return s

def load_log(path):
    with open(path) as f:
        reader = csv.DictReader(f)
        return [{k: try_parse_value(v) for k, v in r.items()} for r in reader if None not in r.values()]

def invalid_offsets(log, modulo):
    slots = []
    off = {}
    t = [r for r in log if r['transmitted']]
    for i in range(len(t) - 1):
        diff = (t[i+1]['slot_num'] - t[i]['slot_num']) % modulo
        if diff != 0:
            slots.append(t[i+1])
            if diff in off:
                off[diff] += 1
            else:
                off[diff] = 1
    return slots, off, len(t) - 1

def show_invalid_offsets(log, modulo):
    slots, off, total = invalid_offsets(log, modulo)
    invalid = len(slots)
    print("{} invalid offsets out of {} ({})".format(invalid, total, float(invalid) / total))
    print(off)

def run_invalid_offsets(path, modulo):
    log = load_log(path)
    show_invalid_offsets(log, modulo)

def invalid_slot_nums(log, modulo):
    slots, off, total = invalid_offsets(log, modulo)
    return [s['slot_num'] for s in slots]

def run_invalid_slot_nums(path, modulo):
    log = load_log(path)
    return invalid_slot_nums(log, modulo)

def node_for_path(path):
    m = re.search(r'\d{4}-\d{2}-\d{2}-([a-zA-Z0-9]+)(?:-|\.)\d+\.csv', path)
    if m is None:
        raise Exception("Unexpected log naming convention.")
    return m.group(1)

def plot_tdma(*paths, offsets=None, modulo=4, slot_time=2200):
    logs = [load_log(path) for path in paths]
    if offsets is None:
        offsets = find_offsets(*logs)
        print("Offsets are {0}".format(offsets))

    symbols = ["bo", "ro", "go", "yo"]
    assert(len(symbols) >= len(paths))

    x_max = 0.0
    for i, path in enumerate(paths):
        x = [(r["slot_num"] + offsets[i]) * (slot_time / 1e6) for r in logs[i] if r["transmitted"] == 1 and r["transmit_success"] == 1]
        y = [(r["slot_num"] + offsets[i]) % modulo for r in logs[i] if r["transmitted"] == 1 and r["transmit_success"] == 1]
        plt.plot(x, y, symbols[i], label=node_for_path(path))
        if len(x) > 0:
            x_max = max(x_max, max(x))

    plt.legend()
    plt.axis([0.0, x_max + 1.0, -1, modulo])
    plt.show()

class TSFSeries:
    def __init__(self, log, threshold=100000):
        self.series = []
        if len(log) == 0:
            return
        current_series = []
        prev = log[0]["tsf_time"]
        for entry in log:
            if abs(entry["tsf_time"] - prev) > threshold:
                self.series.append(current_series)
                current_series = []
            if entry["tsf_time"] != prev:
                current_series.append(entry["tsf_time"])
                prev = entry["tsf_time"]
        self.series.append(current_series)

    def __str__(self):
        entries = []
        for series in self.series:
            if len(series) == 1:
                entries.append(str(series[0]))
            elif len(series) == 2:
                entries.append(str(series[0]))
                entries.append(str(series[1]))
            else:
                entries.append(str(series[0]))
                entries.append('...')
                entries.append(str(series[-1]))
        return '\n'.join(entries)

def map_slots_to_tsf(log, slot_time=2200, threshold=100000):
    '''TSF counter is read only every 5-7 slots, depending on timing. This function maps slot
    numbers to interpolated TSF counter values.
    '''
    slots = {}
    current_read = log[0]["read_num"]
    for i in range(len(log)):
        if log[i]["read_num"] != current_read:
            j = i - 1
            while j >= 0 and log[j]["read_num"] == current_read:
                slots[log[j]["slot_num"]] = log[j]["tsf_time"] - slot_time * (i - j - 1)
                j -= 1
            if abs(log[i]["tsf_time"] - log[i-1]["tsf_time"]) > threshold:
                return slots
            current_read = log[i]["read_num"]
    return slots


def find_offsets(*logs, slot_time=2200):
    '''Determines the average offset in TSF counter values between equal slots in two different
    log files, divides by the slot time, and rounds to an integer to determine the most likely
    relative offset between the slot numbering on the different nodes.
    '''
    baseline = map_slots_to_tsf(logs[0], slot_time=slot_time)
    estimate_offsets = []
    for i in range(len(logs)):
        if i == 0:
            estimate_offsets.append(0)
            continue
        node_slots = map_slots_to_tsf(logs[i], slot_time=slot_time)
        total = 0.0
        count = 0
        for slot, tsf in baseline.items():
            if slot in node_slots:
                total += node_slots[slot] - tsf
                count += 1
        average = total / count
        offset = average / slot_time
        print("Average TSF offset is {0}. Estimated slot offset is {1} slots.".format(average, offset))
        estimate_offsets.append(int(round(offset)))

    return estimate_offsets

def count_transmit_success(log):
    count = 0
    for entry in log:
        if entry["transmitted"] == 1 and entry["transmit_success"] == 1:
            count += 1
    return count
