from agv_network_builder import main as benchmark_builder
import random
import subprocess
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
REPETITIONS = 100


def build_benchmark(rel: float, seed: int):
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
        "-suffix",
        str(rel),
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
        build_benchmark(rel, seed)


def start_benchmark(rel: float, sti: bool):
    if sti:
        return subprocess.Popen(
            [
                "./benchmarks/heuristic",
                "-n",
                f"../data/network{rel}.json",
                "-s",
                f"../data/streams{rel}.json",
                "-sti",
            ],
            stdout=subprocess.PIPE,
        )
    else:
        return subprocess.Popen(
            [
                "./benchmarks/heuristic",
                "-n",
                f"../data/network{rel}.json",
                "-s",
                f"../data/streams{rel}.json",
            ],
            stdout=subprocess.PIPE,
        )


def get_result(out: str):
    return int(out.decode("utf-8").splitlines()[-1].split(" ")[1])


def run_benchmarks():
    cwd = os.getcwd()
    res = {"STI": [0] * len(TESTED_RELIABILITY), "FIPS": [0] * len(TESTED_RELIABILITY)}
    for r in range(REPETITIONS):
        os.chdir(cwd)
        build_benchmarks()

        os.chdir("release")
        handles = {}
        for sti in [False, True]:
            for rel in TESTED_RELIABILITY:
                handles[(sti, rel)] = start_benchmark(rel, sti)

        for key, handle in handles.items():
            i = TESTED_RELIABILITY.index(key[1])
            out, errs = handle.communicate()
            if key[0]:
                res["STI"][i] += get_result(out)
            else:
                res["FIPS"][i] += get_result(out)

        intermediate_res = copy.deepcopy(res)

        for i in range(len(TESTED_RELIABILITY)):
            intermediate_res["STI"][i] = int(intermediate_res["STI"][i] / (r + 1))
            intermediate_res["FIPS"][i] = int(intermediate_res["FIPS"][i] / (r + 1))

        df = pd.DataFrame(data=intermediate_res, index=TESTED_RELIABILITY)
        print("Average results after repetition:", r)
        print(df)


if __name__ == "__main__":
    sys.exit(run_benchmarks())
