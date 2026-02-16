from agv_network_builder import main as benchmark_builder
import agv_network_builder as agv
import subprocess
import itertools
import os
import json
import random
import sys
import datetime
import pandas as pd
import numpy as np

# Benchmark Parameters
REPETITIONS = 10

AGV_WT_OUT = 40
AGV_WT_IN = 40
AGV_CT = 20
CORE_CT = 20

agv.DATA_RATE = 100000000  # 100Mbps
agv.PROPAGATION_DELAY = 50  # 50ns (~10m Ethernet cable)
agv.PROCESSING_DELAY = 0

agv.CT_TYPES = 1
agv.CT_PERIOD = [5000000]
agv.CT_PHASE = [0]
agv.CT_FRAMESIZE = [100]
agv.CT_E2E_LATENCY = [500000]
agv.CT_JITTER = [0]
agv.CT_PCP = [6]

agv.WT_TYPES = 2
agv.WT_RANDOM_WEIGHTS = [1, 1]
agv.WT_PERIOD = [20000000, 20000000]
agv.WT_PHASE = [0, 0]
agv.WT_FRAMESIZE = [100, 100]
agv.WT_STABLE_LATENCY = [20000000, 20000000]
agv.WT_STABLE_JITTER = [0, 0]
agv.WT_MK_FIRM_MASK = ["0", "1"]  # placeholder, modified below
agv.WT_MK_FIRM_LATENCY = [20000000, 20000000]
agv.WT_PCP = [5, 5]

agv.WT_RELIABILITY = [0.99, 0.99]
agv.WT_RTI_POLICY = "minimize_dmax"
agv.STREAM_OBJECTIVE = "tardiness"

WT_MKFIRM_K = 12


def build_plain_benchmark():
    args = [
        "-agv_wt_out",
        str(AGV_WT_OUT),
        "-agv_wt_in",
        str(AGV_WT_IN),
        "-agv_ct",
        str(AGV_CT),
        "-core_ct",
        str(CORE_CT),
        "-q",
    ]
    benchmark_builder(args)


def build_first_fit_mu_pattern(m: int, k: int):
    with open("data/streams.json") as f:
        streams = json.load(f)

    for stream in streams:
        if "mk_firm" not in stream or stream["mk_firm"]["mask"] == "0":
            continue

        stream["mk_firm"]["mask"] = ("1" * m) + ("0" * (k - m))

    with open(f"data/streams_first_fit.json", "w") as f:
        json.dump(streams, f, indent=4)


def build_random_mu_pattern(m: int, k: int):
    with open("data/streams.json") as f:
        streams = json.load(f)

    mask = list(("1" * m) + ("0" * (k - m)))

    for stream in streams:
        if "mk_firm" not in stream or stream["mk_firm"]["mask"] == "0":
            continue

        random.shuffle(mask)
        stream["mk_firm"]["mask"] = "".join(mask)

    with open(f"data/streams_random.json", "w") as f:
        json.dump(streams, f, indent=4)


def build_best_fit_mu_pattern(m: int, k: int):
    with open("data/streams.json") as f:
        streams = json.load(f)

    elevated_traffic_per_link = {}

    for stream in streams:
        if "mk_firm" not in stream or stream["mk_firm"]["mask"] == "0":
            continue

        mask = [0] * k
        max_mu = np.array([0] * k)

        for link in stream["route"]:
            link = f"({link[0]}, {link[1]})"
            if link in elevated_traffic_per_link:
                max_mu = np.maximum(max_mu, elevated_traffic_per_link[link])

        for i in range(m):
            j = np.where(max_mu == min(max_mu))[0][0]
            mask[j] = 1
            max_mu[j] = sys.maxsize

        for link in stream["route"]:
            link = f"({link[0]}, {link[1]})"
            if link in elevated_traffic_per_link:
                elevated_traffic_per_link[link] += max_mu
            else:
                elevated_traffic_per_link[link] = max_mu

        stream["mk_firm"]["mask"] = "".join([str(v) for v in mask])

    with open(f"data/streams_best_fit.json", "w") as f:
        json.dump(streams, f, indent=4)


def build_benchmarks(m: int, k: int):
    build_plain_benchmark()

    build_first_fit_mu_pattern(m, k)
    build_random_mu_pattern(m, k)
    build_best_fit_mu_pattern(m, k)


def start_benchmark(variant: str):
    return subprocess.Popen(
        [
            "./benchmarks/mk_firm",
            "-n",
            f"../data/network.json",
            "-s",
            f"../data/streams_{variant}.json",
        ],
        stdout=subprocess.PIPE,
    )


def get_result(out: str):
    return int(out.decode("utf-8").splitlines()[-1].split(" ")[1])


def run_benchmarks():
    BENCHMARKS = list(range(WT_MKFIRM_K + 1))
    VARIANTS = ["first_fit", "random", "best_fit"]

    cwd = os.getcwd()
    res = {v: [0] * len(BENCHMARKS) for v in VARIANTS}

    for r in range(REPETITIONS):
        for m in BENCHMARKS:
            handles = {}

            os.chdir(cwd)
            build_benchmarks(m, WT_MKFIRM_K)
            os.chdir("release")

            for v in VARIANTS:
                handles[v] = start_benchmark(v)

            for v, handle in handles.items():
                out, errs = handle.communicate()

                res[v][m] += get_result(out)

        intermediate_results = {v: [0] * len(BENCHMARKS) for v in VARIANTS}
        for v in VARIANTS:
            for m in BENCHMARKS:
                intermediate_results[v][m] = int(res[v][m] / (r + 1))

        print("Average results after repetition:", r)
        df = pd.DataFrame.from_dict(data=intermediate_results)
        print(df)


if __name__ == "__main__":
    sys.exit(run_benchmarks())
