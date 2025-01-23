from agv_network_builder import main as benchmark_builder, WT_JITTER
from omnetpp_generator import main as omnetpp_generator, STREAM_TO_MODULE_MAP
import subprocess
import shutil
import os
import json
import itertools
import math
import sys
import pandas as pd
from omnetpp.scave import results

AGV_WT_OUT = 50
AGV_WT_IN = 50
AGV_CT = 5
CORE_CT = 5

LOW_RELIABILITY = 0.5
HIGH_RELIABILITY = 0.9999
JITTER = 100000

REPETITIONS = 1000
SIM_TIME = 20  # 20s = 1000 hypercycles
SIM_BATCHES = 10  # run X repetitions of each simulation in parallel

HIGH_CRITICALITY_STREAMS = [f"AGV0_CORE_0{i}" for i in range(5)] + [
    f"CORE_AGV0_0{i}" for i in range(5)
]

PACKAGE_NAME = "agv"
D6G_PATH = "/usr/src/omnetpp/workspace/deterministic6g"
INET_PATH = "/usr/src/omnetpp/workspace/inet"

SIMULATIONS = ["FIPS", "SCALAR_MEDIAN", "SCALAR_MAX"]
STREAM_TO_APPS = {t: {} for t in SIMULATIONS}


def change_reliability(t, stream_names, reliability):
    with open(f"data/simulations/streams_{t}.json", "r") as f:
        d = json.load(f)

    for stream in d:
        if stream["name"] in stream_names:
            stream["pdb_map"][0]["reliability"] = reliability

    with open(f"data/simulations/streams_{t}.json", "w") as f:
        json.dump(d, f, indent=4)


def build_benchmark(rel: float, jitter=WT_JITTER, pdc=0, name=""):
    import agv_network_builder as agv

    agv.WT_PCP = [7]
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
        "-pdc",
        str(pdc),
        "-vslot",
        "1",
        "-prefix",
        "data/simulations",
        "-suffix",
        f"_{name}",
        "-q",
    ]
    benchmark_builder(args)


def generate_omnetini(t=""):
    args = [
        "-t",
        f"data/simulations/network_{t}.json",
        "-s",
        f"data/simulations/streams_{t}.json",
        "-g",
        f"data/simulations/tsn_configuration_{t}.json",
        "-ned",
        "data/simulations/network.ned",
        "-ini",
        f"data/simulations/omnetpp_{t}.ini",
        "--scenario",
        t,
        "--package_name",
        PACKAGE_NAME,
        "--network_name",
        "AGVNetwork",
        "--simulation_time",
        str(SIM_TIME),
        "--repetitions",
        str(REPETITIONS),
        "--histogram_directory",
        "histograms",
    ]
    omnetpp_generator(args)


def get_expected_arrival_interval(t):
    with open(f"data/simulations/tsn_configuration_{t}.json", "r") as f:
        d = dict(json.load(f))
        return d["LISTENERS"]


def get_talker_offsets(t):
    with open(f"data/simulations/tsn_configuration_{t}.json", "r") as f:
        d = dict(json.load(f))
        return d["TALKERS"]


def strip_psfp(t=""):
    with open(f"data/simulations/tsn_configuration_{t}.json", "r") as f:
        d = dict(json.load(f))
    d["PSFP"] = {}
    with open(f"data/simulations/tsn_configuration_{t}.json", "w") as f:
        json.dump(d, f, indent=4)


def generate_full_omnetini():
    if not os.path.exists("data/simulations"):
        os.mkdir("data/simulations")
    build_benchmark(LOW_RELIABILITY, JITTER, 0, "FIPS")
    change_reliability("FIPS", HIGH_CRITICALITY_STREAMS, HIGH_RELIABILITY)

    # scalar approaches are unable to configure reliability
    build_benchmark(0.5, JITTER, 0.5, "SCALAR_MEDIAN")
    build_benchmark(1, JITTER, 1, "SCALAR_MAX")

    cwd = os.getcwd()
    os.chdir("release")
    handles = []
    for t in ["FIPS", "SCALAR_MEDIAN", "SCALAR_MAX"]:
        handles.append(
            subprocess.Popen(
                [
                    "./benchmarks/heuristic",
                    "-n",
                    f"../data/simulations/network_{t}.json",
                    "-s",
                    f"../data/simulations/streams_{t}.json",
                    "-o",
                    f"../data/simulations/tsn_configuration_{t}.json",
                ],
                stdout=subprocess.PIPE,
            )
        )

    for handle in handles:
        handle.communicate()

    os.chdir(cwd)
    strip_psfp("SCALAR_MEDIAN")
    strip_psfp("SCALAR_MAX")
    if not os.path.exists(f"{D6G_PATH}/simulations/{PACKAGE_NAME}"):
        os.mkdir(f"{D6G_PATH}/simulations/{PACKAGE_NAME}")
    shutil.copytree(
        "data/histograms",
        f"{D6G_PATH}/simulations/{PACKAGE_NAME}/histograms",
        dirs_exist_ok=True,
    )
    for t in ["FIPS", "SCALAR_MEDIAN", "SCALAR_MAX"]:
        generate_omnetini(t)
        STREAM_TO_APPS[t] = STREAM_TO_MODULE_MAP.copy()
        for f in ["network.ned", f"omnetpp_{t}.ini"]:
            shutil.copy(
                f"data/simulations/{f}",
                f"{D6G_PATH}/simulations/{PACKAGE_NAME}/{f}",
            )


def run_simulation():
    generate_full_omnetini()

    res = {t: {"stream": [], "min": [], "max": [], "count": []} for t in SIMULATIONS}
    expected_arrival = {t: get_expected_arrival_interval(t) for t in SIMULATIONS}
    talker_offset = {t: get_talker_offsets(t) for t in SIMULATIONS}

    os.chdir(f"{D6G_PATH}/simulations/{PACKAGE_NAME}")
    for repetition in range(int(REPETITIONS / SIM_BATCHES)):
        handles = []
        for t, r1 in itertools.product(SIMULATIONS, range(SIM_BATCHES)):
            handles.append(
                # ../../src/deterministic6g -u Cmdenv -m -r ${i} -c ${s} -n ${d6g_path}/simulations:${d6g_path}/src:${inet_path}/src -l ${inet_path}/src/INET omnetpp.ini
                subprocess.Popen(
                    [
                        f"{D6G_PATH}/src/deterministic6g",
                        "-u",
                        "Cmdenv",
                        "-m",
                        "-r",
                        str(repetition * SIM_BATCHES + r1),
                        "-c",
                        t,
                        "-n",
                        f"{D6G_PATH}/simulations:{D6G_PATH}/src:{INET_PATH}/src",
                        "-l",
                        f"{INET_PATH}/src/INET",
                        f"omnetpp_{t}.ini",
                    ],
                    stdout=subprocess.PIPE,
                )
            )

        for handle in handles:
            handle.communicate()

        for t, r1 in itertools.product(SIMULATIONS, range(SIM_BATCHES)):
            result_files = [
                f"results/{t}-#{repetition * SIM_BATCHES + r1}.vec",
                f"results/{t}-#{repetition * SIM_BATCHES + r1}.sca",
            ]
            r = results.read_result_files(
                result_files, "name =~ meanBitLifeTimePerPacket:vector"
            )
            df = results.get_results(r, row_types=["vector"])
            if not res[t]["stream"]:
                res[t]["stream"] = list(STREAM_TO_APPS[t].keys())
                res[t]["min"] = [
                    [math.inf] * len(STREAM_TO_APPS[t][a]) for a in res[t]["stream"]
                ]
                res[t]["max"] = [
                    [0] * len(STREAM_TO_APPS[t][a]) for a in res[t]["stream"]
                ]
                res[t]["count"] = [
                    [0] * len(STREAM_TO_APPS[t][a]) for a in res[t]["stream"]
                ]

            for stream_id, (stream, apps) in enumerate(STREAM_TO_APPS[t].items()):
                for i, app in enumerate(apps):
                    if app not in list(df["module"]):
                        # omnet does not populate list if no frames arrived at all
                        continue
                    j = list(df["module"]).index(app)
                    arrival_times = df["vecvalue"][j]
                    res[t]["max"][stream_id][i] = round(
                        max(max(arrival_times), res[t]["max"][stream_id][i] / 1000)
                        * 1000,
                        3,
                    )
                    res[t]["min"][stream_id][i] = round(
                        min(min(arrival_times), res[t]["min"][stream_id][i] / 1000)
                        * 1000,
                        3,
                    )
                    for latency in arrival_times:
                        rx = talker_offset[t][f"{stream}#{i}"] + round(latency * 1e9)
                        if (
                            expected_arrival[t][f"{stream}#{i}"][0] <= rx
                            and rx <= expected_arrival[t][f"{stream}#{i}"][1]
                        ):
                            res[t]["count"][stream_id][i] += 1
                        else:
                            pass

        for t in SIMULATIONS:
            print("\n", t, "\n", "-" * 50)
            print(pd.DataFrame(data=res[t]).to_string())

        shutil.rmtree("results")


if __name__ == "__main__":
    sys.exit(run_simulation())
