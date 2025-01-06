from agv_network_builder import main as benchmark_builder, WT_JITTER
import subprocess
import itertools
import os
import copy
import sys
import datetime
import pandas as pd

AGV_WT_OUT = 200
AGV_WT_IN = 200
AGV_CT = 15
CORE_CT = 15

TESTED_RELIABILITY = [0.9, 0.99, 0.999, 0.9999]
TESTED_JITTER = [1000, 20000, 40000, 60000, 80000, 100000]
REPETITIONS = 100


def build_benchmark(rel: float, seed: int, jitter=WT_JITTER):
    args = [
        "-agv_wt_out",
        str(AGV_WT_OUT),
        "-agv_wt_in",
        str(AGV_WT_IN),
        "-agv_ct",
        str(AGV_CT),
        "-core_ct",
        str(CORE_CT),
        "-rel",
        str(rel),
        "-jitter",
        str(jitter),
        "-suffix",
        f"{rel}_{jitter}",
        "-seed",
        str(seed),
        "-q",
    ]
    benchmark_builder(args)


def build_benchmarks():
    # use the same seed for all tested reliability requirements
    # this ensures the same stream set for all tests
    seed = int(datetime.datetime.now().timestamp())
    for rel in TESTED_RELIABILITY:
        for jitter in TESTED_JITTER:
            build_benchmark(rel, seed, jitter)


def start_benchmark(rel: float, sti: bool, jitter=WT_JITTER):
    if sti:
        return subprocess.Popen(
            [
                "./benchmarks/heuristic",
                "-n",
                f"../data/network{rel}_{jitter}.json",
                "-s",
                f"../data/streams{rel}_{jitter}.json",
                "-sti",
            ],
            stdout=subprocess.PIPE,
        )
    else:
        return subprocess.Popen(
            [
                "./benchmarks/heuristic",
                "-n",
                f"../data/network{rel}_{jitter}.json",
                "-s",
                f"../data/streams{rel}_{jitter}.json",
            ],
            stdout=subprocess.PIPE,
        )


def get_result(out: str):
    return int(out.decode("utf-8").splitlines()[-1].split(" ")[1])


def run_benchmarks():
    BENCHMARKS = list(itertools.product(TESTED_RELIABILITY, TESTED_JITTER))

    cwd = os.getcwd()
    res = {"STI": [0] * len(BENCHMARKS), "FIPS": [0] * len(BENCHMARKS)}
    for r in range(REPETITIONS):
        os.chdir(cwd)
        build_benchmarks()

        os.chdir("release")
        handles = {}
        for sti in [False, True]:
            for rel, jitter in BENCHMARKS:
                handles[(sti, (rel, jitter))] = start_benchmark(rel, sti, jitter)

        for key, handle in handles.items():
            i = BENCHMARKS.index(key[1])
            out, errs = handle.communicate()
            if key[0]:
                res["STI"][i] += get_result(out)
            else:
                res["FIPS"][i] += get_result(out)

        intermediate_res = copy.deepcopy(res)

        for i in range(len(BENCHMARKS)):
            intermediate_res["STI"][i] = int(intermediate_res["STI"][i] / (r + 1))
            intermediate_res["FIPS"][i] = int(intermediate_res["FIPS"][i] / (r + 1))

        df = pd.DataFrame(data=intermediate_res, index=BENCHMARKS)
        print("Average results after repetition:", r)
        print(df)


if __name__ == "__main__":
    sys.exit(run_benchmarks())
