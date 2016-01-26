#!/usr/bin/env python3
'''Utilities for analysis of Metamac log files.
Author: Nathan Flick
'''

import csv
from math import *
import re
from glob import glob

import numpy as np
import matplotlib.pyplot as plt

def parse_value(s):
    try:
        return int(s)
    except ValueError:
        try:
            return float(s)
        except ValueError:
            return s

class Log:
    def __init__(self, path, slot_time=2200, tsf_threshold=200000):
        self.path = path
        self.time_offset = 0.0
        self.slot_offset = 0
        m = re.search(r'alix\d+', path)
        if m:
            self.node = m.group()
        else:
            self.node = 'unknown'
        with open(path) as f:
            self.header = f.readline().strip().split(',')
            f.seek(0)
            reader = csv.DictReader(f)
            self.entries = [{k: parse_value(v) for k, v in r.items()} for r in reader if None not in r.values()]
        self.protocols = self.header[self.header.index('protocol')+1:]

        self.first_jump = None
        self.tsf_segments = []
        if len(self.entries) > 0:
            current_segment = [self.entries[0]["tsf_time"]]
            prev = current_segment[0]
            for i, entry in enumerate(self.entries):
                if abs(entry["tsf_time"] - prev) > tsf_threshold:
                    if self.first_jump is None:
                        self.first_jump = i
                    self.tsf_segments.append(current_segment)
                    current_segment = []
                if entry["tsf_time"] != prev:
                    current_segment.append(entry["tsf_time"])
                    prev = entry["tsf_time"]
            self.tsf_segments.append(current_segment)
            self.tsf_jumps = len(self.tsf_segments) > 1
        else:
            self.tsf_jumps = False

        current_read = self.entries[0]["read_num"]
        for i in range(len(self.entries)):
            if self.entries[i]["read_num"] != current_read:
                j = i - 1
                while j >= 0 and self.entries[j]["read_num"] == current_read:
                    self.entries[j]["tsf_interp"] = self.entries[j]["tsf_time"] - slot_time * (i - j - 1)
                    j -= 1
                current_read = self.entries[i]["read_num"]
        j = len(self.entries) - 1
        while j >= 0 and self.entries[j]["read_num"] == current_read:
            self.entries[j]["tsf_interp"] = self.entries[j]["tsf_time"] - slot_time * (len(self.entries) - j - 1)
            j -= 1

    def print_tsf_segments(self):
        for segment in self.tsf_segments:
            if len(segment) == 1:
                print(segment[0])
            elif len(segment) == 2:
                print(segment[0])
                print(segment[1])
            else:
                print(segment[0])
                print('...')
                print(segment[-1])

    def slot_num(self, num, hi=None):
        if hi is None:
            hi = len(self.entries) - 1
        lo = 0
        while lo < hi:
            mid = (lo + hi) >> 1
            if self.entries[mid]["slot_num"] < num:
                lo = mid + 1
            else:
                hi = mid
        if lo == hi and self.entries[lo]["slot_num"] == num:
            return self.entries[lo]
        return None

class Experiment:
    def __init__(self, globpattern, access_point=None, slot_time=2200, tsf_threshold=200000):
        self.access_point = access_point
        self.slot_time = slot_time
        self.logs = [Log(p, slot_time=slot_time, tsf_threshold=tsf_threshold) for p in glob(globpattern)]
        if len(self.logs) == 0:
            raise Exception('No logs found')
        self.tsf_jumps = False
        for log in self.logs:
            self.tsf_jumps |= log.tsf_jumps

        if self.access_point is None:
            baseline = self.logs[0]
        else:
            baseline = next(l for l in self.logs if l.node == self.access_point)

        for log in self.logs:
            total = 0.0
            count = 0
            for entry in baseline.entries[:baseline.first_jump]:
                slot = log.slot_num(entry["slot_num"], log.first_jump - 1)
                if slot is not None:
                    total += slot["tsf_interp"] - entry["tsf_interp"]
                    count += 1
            log.time_offset = total / count
            log.slot_offset = int(round(log.time_offset / slot_time))

    def tdma_assign_for_slot(self, slot):
        m = re.match(r'TDMA \(slot (\d+)\)', slot['protocol'])
        if not m:
            raise Exception('Unexpected naming convention for TDMA protocol.')
        proto = m.group(1)
        return int(proto)

    def plot_tdma(self, modulo=None, slot_time=2200, all_xmit=False):
        if modulo is None:
            modulo = len(self.logs)
        if self.tsf_jumps:
            print("TSF jump behavior present!")
        #for log in logs:
        #    interpolate_tsf(log)
        symbols = ["bo", "ro", "go", "yo"]
        assert(len(symbols) >= len(self.logs))

        x_max = 0.0
        fig = plt.figure()
        ax = plt.subplot(111)
        for i, log in enumerate(self.logs):
            nudge = .5 * (i + 1) / (len(self.logs) + 1)
            x = [(r["slot_num"] + log.slot_offset) * (self.slot_time * 1e-6) for r in log.entries if r["transmitted"] == 1 and (all_xmit or r["transmit_success"] == 1)]
            y = [((r["slot_num"] + log.slot_offset) % modulo) - .25 + nudge for r in log.entries if r["transmitted"] == 1 and (all_xmit or r["transmit_success"] == 1)]
            ax.plot(x, y, symbols[i], label=log.node)
            if len(x) > 0:
                x_max = max(x_max, max(x))

        box = ax.get_position()
        ax.set_position([box.x0, box.y0, box.width * 0.8, box.height])
        ax.legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax.axis([0.0, x_max + 1.0, -1, modulo])
        plt.show()

def count(logs, *predicates):
    count = 0
    for log in logs:
        for entry in log:
            satisfies = True
            for predicate in predicates:
                if entry[predicate] != 1:
                    satisfies = False
                    break
            if satisfies:
                count += 1
    return count

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

