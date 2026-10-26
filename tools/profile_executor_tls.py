#!/usr/bin/env python3
"""Attribute dynamic TLS samples to their nearest recorded caller (NDK simpleperf)."""
import argparse
import collections
import json
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("profile")
parser.add_argument("--simpleperf-dir", required=True)
parser.add_argument("--tid", type=int, required=True)
args = parser.parse_args()
sys.path.insert(0, args.simpleperf_dir)
from simpleperf_report_lib import GetReportLib

report = GetReportLib(args.profile)
total = tls = missing = samples = tls_samples = 0
callers = collections.Counter()
sites = collections.Counter()
try:
    while (sample := report.GetNextSample()) is not None:
        if sample.tid != args.tid:
            continue
        total += sample.period
        samples += 1
        symbol = report.GetSymbolOfCurrentSample()
        if "tlsdesc_resolver_dynamic" not in symbol.symbol_name:
            continue
        tls += sample.period
        tls_samples += 1
        chain = report.GetCallChainOfCurrentSample()
        caller = next((chain.entries[i].symbol for i in range(chain.nr)
                       if "tlsdesc_resolver_dynamic" not in
                       chain.entries[i].symbol.symbol_name), None)
        if caller is None:
            missing += sample.period
            continue
        callers[caller.symbol_name] += sample.period
        sites[(caller.symbol_name, hex(caller.vaddr_in_file))] += sample.period
finally:
    report.Close()

def shares(period):
    return {"period": period,
            "percent_executor": 100 * period / total if total else 0,
            "percent_tls": 100 * period / tls if tls else 0}

print(json.dumps({
    "tid": args.tid, "samples": samples, "tls_samples": tls_samples,
    "total_period": total, "tls_period": tls,
    "unattributed_period": missing,
    "callers": [{"symbol": name, **shares(value)}
                for name, value in callers.most_common()],
    "sites": [{"symbol": key[0], "vaddr": key[1], **shares(value)}
              for key, value in sites.most_common()],
}, indent=2))
