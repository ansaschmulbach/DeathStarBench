#!/usr/bin/env python3
"""Reconstructs a real, correlated timeline from one ghost-scheduled thread's
sched_yield() call through the other thread's sched_yield() return, using the
two files timeline_same_process_debug.sh produces:

  agent_log   -- ab_alternator_agent's stderr with GHOST_TIMELINE_DEBUG=1:
                 [ab_alternator] task <name>/<tid>/<gtid> is A/B    (once per task)
                 [timeline] cpu=.. gtid=.. avail_done=..ns dequeue=[..,..](+..)
                     txn_open=..(+..) commit=[..,..](+..) task_on_cpu=..(+..)
                 [timeline-msg] ns=.. type=.. gtid=.. empty_streak=..
                 [timeline-bench] avg_empty_peek_ns=..
                 (all buffered and dumped once at agent shutdown -- see
                 DumpTimelineRecords() in ab_scheduler.cc -- so none of this
                 costs a per-event write() syscall during the actual run)

  shm_timeline -- CombinedService's shm_log.h timeline via Logger, e.g.
                 t0_ns=<absolute CLOCK_MONOTONIC ns for rel_time_us=0>
                 rel_time_us  cpu  pid  tid  label  seq
                 <rows...>

Both clocks are CLOCK_MONOTONIC (ghost-userspace's MonotonicNow() and
shm_log.h's clock_gettime(CLOCK_MONOTONIC) call) and directly comparable --
confirmed by a same-process cross-check that agreed to within 67ns.

Why "robust pairing" instead of just zipping the two sequences by index: the
agent occasionally has more commit events for a label than that label has
real requests -- PickNextGlobalCPU() (see the ab-alternator-affinity-fix
branch's commit message) can still preempt a worker on rare occasions even
with the escape-valve cpu, and a preempted-then-resumed task gets a second,
real commit/dispatch with no new shm "_start" (it's resuming a request whose
"_start" was already logged, not beginning a new one). A naive index-zip
lets that one extra event shift every pairing after it; this instead
advances a pointer through the commit sequence and, for each shm "_start",
keeps whichever commit immediately precedes it, silently absorbing any extra
commits in between.

Usage:
  python3 analyze_yield_timeline.py <agent_log> <shm_timeline> [--drop N]

--drop N (default 500): requests dropped from each end of the run before
computing statistics, to exclude STARTUP_DELAY_MS warmup and any drain-phase
tail effects (e.g. one side's thread finishing before the other).
"""
import argparse
import re
import sys

DISPATCH_PAT = re.compile(
    r'\[timeline\] cpu=(\d+) gtid=(\d+) avail_done=(\d+)ns '
    r'dequeue=\[(\d+)ns,(\d+)ns\]\(\+\d+ns\) '
    r'txn_open=(\d+)ns\(\+\d+ns\) '
    r'commit=\[(\d+)ns,(\d+)ns\]\(\+\d+ns\) '
    r'task_on_cpu=(\d+)ns'
)
TASK_PAT = re.compile(r'\[ab_alternator\] task \S+/(\d+)/(\d+) is [AB]')
MSG_PAT = re.compile(r'\[timeline-msg\] ns=(\d+) type=(\d+) gtid=(\d+) empty_streak=(\d+)')
BENCH_PAT = re.compile(r'\[timeline-bench\] avg_empty_peek_ns=([\d.]+)')

MSG_TYPE_NAMES = {
    64: 'TASK_DEAD', 65: 'TASK_BLOCKED', 66: 'TASK_WAKEUP', 67: 'TASK_NEW',
    68: 'TASK_PREEMPT', 69: 'TASK_YIELD', 70: 'TASK_DEPARTED',
    71: 'TASK_SWITCHTO', 72: 'TASK_AFFINITY_CHANGED', 73: 'TASK_ON_CPU',
}


def parse_agent_log(path):
    gtid_to_label = {}
    dispatches = {}  # label -> list of dicts, in file order
    msg_seen = []  # list of (ns, type, gtid)
    avg_peek_ns = None

    with open(path) as f:
        for line in f:
            m = TASK_PAT.search(line)
            if m:
                tid, gtid = int(m.group(1)), int(m.group(2))
                label = 'uid' if not gtid_to_label else None  # placeholder, fixed below
                continue
    # Two passes: first collect (tid, gtid) in order of appearance, first is
    # "A", second is "B" -- but we want *semantic* labels (uid/media), which
    # only the DeathStarBench side knows (it's just "A"/"B" to the agent).
    # Without cross-referencing CombinedService's own log we can't recover
    # "uid" vs "media" specifically, so labels here are generic Thread-A /
    # Thread-B; that's fine for the timeline itself.
    gtid_order = []
    with open(path) as f:
        for line in f:
            m = TASK_PAT.search(line)
            if m:
                tid, gtid = int(m.group(1)), int(m.group(2))
                if gtid not in gtid_order:
                    gtid_order.append(gtid)
    for i, gtid in enumerate(gtid_order):
        gtid_to_label[gtid] = 'threadA' if i == 0 else 'threadB'
        dispatches[gtid_to_label[gtid]] = []

    with open(path) as f:
        for line in f:
            m = DISPATCH_PAT.search(line)
            if m:
                gtid = int(m.group(2))
                label = gtid_to_label.get(gtid)
                if label:
                    dispatches[label].append({
                        'cpu': int(m.group(1)),
                        'avail_done': int(m.group(3)),
                        'dequeue_start': int(m.group(4)), 'dequeue_done': int(m.group(5)),
                        'txn_open': int(m.group(6)),
                        'commit_start': int(m.group(7)), 'commit_done': int(m.group(8)),
                        'task_on_cpu': int(m.group(9)),
                    })
                continue
            m = MSG_PAT.search(line)
            if m:
                msg_seen.append({
                    'ns': int(m.group(1)), 'type': int(m.group(2)),
                    'gtid': int(m.group(3)), 'streak': int(m.group(4)),
                })
                continue
            m = BENCH_PAT.search(line)
            if m:
                avg_peek_ns = float(m.group(1))

    return gtid_to_label, dispatches, msg_seen, avg_peek_ns


def parse_shm_timeline(path):
    t0_ns = None
    starts = {}  # label -> [ns, ...] in file order
    ends = {}
    in_section = False
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('t0_ns='):
                t0_ns = int(line.split('=')[1])
                continue
            if line.startswith('=== timeline'):
                in_section = True
                continue
            if line.startswith('=== per-request'):
                break
            if not in_section:
                continue
            if line.startswith('rel_time_us') or line.startswith('  ---') or not line.strip():
                continue
            parts = line.split('\t')
            if len(parts) != 6:
                continue
            rel_us = float(parts[0])
            label = parts[4].strip()
            abs_ns = t0_ns + int(round(rel_us * 1000))
            if label.endswith('_start'):
                base = label[:-6]
                starts.setdefault(base, []).append(abs_ns)
            elif label.endswith('_end'):
                base = label[:-4]
                ends.setdefault(base, []).append(abs_ns)
    return t0_ns, starts, ends


def robust_pair(a_sorted, b_sorted):
    """For each b in b_sorted, pair it with the nearest-preceding a, then
    advance past that a -- so extra a's in between get silently skipped
    rather than shifting every later pairing (see module docstring)."""
    pairs = []
    i = 0
    for b in b_sorted:
        while i + 1 < len(a_sorted) and a_sorted[i + 1] < b:
            i += 1
        if i < len(a_sorted) and a_sorted[i] < b:
            pairs.append((a_sorted[i], b))
            i += 1
    return pairs


def pct(sorted_vals, p):
    return sorted_vals[min(len(sorted_vals) - 1, int(len(sorted_vals) * p))]


def print_stats(label, vals_ns, unit='us', scale=1000.0):
    v = sorted(vals_ns)
    n = len(v)
    if n == 0:
        print(f'  {label}: (no data)')
        return
    print(f'  {label} (n={n}): mean={sum(v)/n/scale:.3f}{unit} '
          f'p10={pct(v,0.1)/scale:.3f}{unit} p50={pct(v,0.5)/scale:.3f}{unit} '
          f'p90={pct(v,0.9)/scale:.3f}{unit} p99={pct(v,0.99)/scale:.3f}{unit} '
          f'max={v[-1]/scale:.3f}{unit}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('agent_log')
    ap.add_argument('shm_timeline')
    ap.add_argument('--drop', type=int, default=500)
    args = ap.parse_args()

    gtid_to_label, dispatches, msg_seen, avg_peek_ns = parse_agent_log(args.agent_log)
    if len(gtid_to_label) != 2:
        print(f'expected exactly 2 tasks in {args.agent_log}, found {len(gtid_to_label)}', file=sys.stderr)
        sys.exit(1)
    t0_ns, shm_starts, shm_ends = parse_shm_timeline(args.shm_timeline)
    if t0_ns is None:
        print(f'no t0_ns= line in {args.shm_timeline} -- rebuild Logger from the '
              'ghost-scheduling-affinity-and-tooling branch or later', file=sys.stderr)
        sys.exit(1)

    shm_labels = sorted(shm_starts.keys())
    if len(shm_labels) != 2:
        print(f'expected exactly 2 shm-log labels, found {shm_labels}', file=sys.stderr)
        sys.exit(1)
    thread_labels = sorted(gtid_to_label.values())
    # Map agent's generic threadA/threadB to the real shm-log labels by
    # matching event counts, since we have no other cross-reference.
    counts = {tl: len(dispatches[tl]) for tl in thread_labels}
    shm_counts = {sl: len(shm_starts[sl]) for sl in shm_labels}
    ordered_threads = sorted(thread_labels, key=lambda t: counts[t])
    ordered_shm = sorted(shm_labels, key=lambda s: shm_counts[s])
    label_map = dict(zip(ordered_threads, ordered_shm))  # threadA/B -> uid/media
    print(f'label mapping (by event count): {label_map}')

    print(f'\n=== per-segment stats (drop={args.drop} from each end) ===')
    all_pairs = {}
    for tl in thread_labels:
        real_label = label_map[tl]
        commits = [d['commit_done'] for d in dispatches[tl]]
        starts = shm_starts[real_label]
        pairs = robust_pair(commits, starts)
        all_pairs[real_label] = pairs
        wake = [s - c for c, s in pairs[args.drop:-args.drop]]
        print_stats(f'{real_label}: commit_done -> shm_start (wakeup latency)', wake)

    print()
    for tl in thread_labels:
        real_label = label_map[tl]
        d = dispatches[tl][args.drop:-args.drop]
        print_stats(f'{real_label}: avail_done -> task_on_cpu (agent processing)',
                    [r['task_on_cpu'] - r['avail_done'] for r in d])
        print_stats(f'  ...commit_start -> commit_done (CommitRunRequests only)',
                    [r['commit_done'] - r['commit_start'] for r in d])

    if msg_seen and avg_peek_ns is not None:
        print(f'\n=== message-drain loop (avg_empty_peek_ns={avg_peek_ns:.2f}) ===')
        for gtid, real_label in [(g, label_map[l]) for g, l in gtid_to_label.items()]:
            streaks = sorted(m['streak'] for m in msg_seen[args.drop:-args.drop] if m['gtid'] == gtid and m['type'] == 69)
            if streaks:
                n = len(streaks)
                p50 = streaks[n // 2]
                print(f'  {real_label} TASK_YIELD empty_streak: p50={p50} '
                      f'(~{p50 * avg_peek_ns:.0f}ns of polling)')

    # Build one representative example: whichever label's dispatch has a
    # full uid_end/media_end-preceding-start span nearest this run's median.
    print(f'\n=== representative example ===')
    label_a, label_b = shm_labels[0], shm_labels[1]
    tl_b = [tl for tl, l in label_map.items() if l == label_b][0]
    ends_a = sorted(shm_ends[label_a])
    candidates = []
    import bisect
    for c, start_b in all_pairs[label_b][args.drop:-args.drop]:
        pos = bisect.bisect_right(ends_a, c) - 1
        if pos < 0:
            continue
        end_a_ns = ends_a[pos]
        # find the matching dispatch record for its full detail
        idx = None
        for i, d in enumerate(dispatches[tl_b]):
            if d['commit_done'] == c:
                idx = i
                break
        if idx is None:
            continue
        candidates.append((start_b - end_a_ns, dispatches[tl_b][idx], end_a_ns, start_b))

    if not candidates:
        print('  (could not build an example -- insufficient overlapping data)')
        return
    spans_sorted = sorted(c[0] for c in candidates)
    target = spans_sorted[len(spans_sorted) // 2]
    _, d, end_a_ns, start_b_ns = min(candidates, key=lambda c: abs(c[0] - target))

    print(f'full span ({label_a}_end -> {label_b}_start) distribution:')
    print_stats('  ', [c[0] for c in candidates])

    t0 = end_a_ns
    events = [
        (end_a_ns, f'{label_a}: LogShmEvent({label_a}_end) -- about to call sched_yield()'),
        (d['avail_done'], 'agent: GlobalSchedule() sees the cpu free (avail_done)'),
        (d['dequeue_start'], 'agent: Dequeue(cpu) call starts'),
        (d['dequeue_done'], f'agent: Dequeue(cpu) returns {label_b} (affinity-checked A/B pick)'),
        (d['txn_open'], 'agent: RunRequest->Open() -- transaction opened'),
        (d['commit_start'], 'agent: CommitRunRequests() call starts'),
        (d['commit_done'], 'agent: CommitRunRequests() returns -- committed to kernel'),
        (d['task_on_cpu'], 'agent: TaskOnCpu() bookkeeping done'),
        (start_b_ns, f'{label_b}: resumes, LogShmEvent({label_b}_start) -- sched_yield() has returned'),
    ]
    events.sort()
    print(f'\n(full span = {(start_b_ns - end_a_ns)/1000:.3f}us, near this run\'s median)\n')
    for ts, desc in events:
        rel_us = (ts - t0) / 1000.0
        print(f't={rel_us:+8.3f}us  {desc}')
    print(f'\ntotal: {label_a}\'s sched_yield call -> {label_b}\'s sched_yield return = '
          f'{(start_b_ns - end_a_ns)/1000.0:.3f}us')


if __name__ == '__main__':
    main()
