from agv_network_builder import main as benchmark_builder
from omnetpp_generator import main as omnetpp_generator, STREAM_TO_MODULE_MAP
from omnetpp_pcap_analysis import main as pcap_analysis
import subprocess
import shutil
import os
import json
import itertools
import math
import sys
import pandas as pd
from omnetpp.scave import results

AGV_WT_OUT = 40
AGV_WT_IN = 40
AGV_CT = 10
CORE_CT = 10

REPETITIONS = 1000
SIM_TIME = 20  # 20s = 1000 hypercycles
SIM_BATCHES = 10  # run X repetitions of each simulation in parallel

PACKAGE_NAME = "mk_firm"
D6G_PATH = "/usr/src/omnetpp/workspace/deterministic6g"
INET_PATH = "/usr/src/omnetpp/workspace/inet"

SIMULATIONS = ["tsn_configuration", "mkfirm_configuration"]
STREAM_TO_APPS = {t: {} for t in SIMULATIONS}


def build_benchmark():
    args = [
        "-agv_wt_out",
        str(AGV_WT_OUT),
        "-agv_wt_in",
        str(AGV_WT_IN),
        "-agv_ct",
        str(AGV_CT),
        "-core_ct",
        str(CORE_CT),
        "-prefix",
        "data/mkfirm_simulations",
        "-q",
    ]
    benchmark_builder(args)


def analyze_pcap(t, run=0):
    os.makedirs("data/mkfirm_simulations/csv", exist_ok=True)
    os.makedirs(f"data/mkfirm_simulations/csv/{t}", exist_ok=True)

    args = [
        "pcap",
        "-t",
        "data/mkfirm_simulations/network.json",
        "-s",
        "data/mkfirm_simulations/streams.json",
        "-in",
        f"{D6G_PATH}/simulations/mk_firm/results/{t}",
        "-out",
        f"data/mkfirm_simulations/csv/{t}",
        "--suffix",
        str(run),
    ]
    pcap_analysis(args)


def generate_omnetini(t=""):
    args = [
        "-t",
        "data/mkfirm_simulations/network.json",
        "-s",
        "data/mkfirm_simulations/streams.json",
        "-g",
        f"data/mkfirm_simulations/{t}.json",
        "-ned",
        "data/mkfirm_simulations/network.ned",
        "-ini",
        f"data/mkfirm_simulations/omnetpp_{t}.ini",
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
        "--delay_outliers",
    ]
    omnetpp_generator(args)


def generate_full_omnetini():
    os.makedirs("data/mkfirm_simulations", exist_ok=True)

    build_benchmark()
    cwd = os.getcwd()
    os.chdir("release")
    handle = subprocess.Popen(
        [
            "./benchmarks/mk_firm",
            "-n",
            "../data/mkfirm_simulations/network.json",
            "-s",
            "../data/mkfirm_simulations/streams.json",
            "--output_normal",
            "../data/mkfirm_simulations/tsn_configuration.json",
            "--output_mkfirm",
            "../data/mkfirm_simulations/mkfirm_configuration.json",
        ],
        stdout=subprocess.PIPE,
    )
    handle.communicate()

    # prepare simulation
    os.chdir(cwd)
    os.makedirs(f"{D6G_PATH}/simulations/{PACKAGE_NAME}", exist_ok=True)
    shutil.copytree(
        "data/histograms",
        f"{D6G_PATH}/simulations/{PACKAGE_NAME}/histograms",
        dirs_exist_ok=True,
    )

    # generate network.ned and omnetpp.ini
    for t in SIMULATIONS:
        generate_omnetini(t)
        STREAM_TO_APPS[t] = STREAM_TO_MODULE_MAP.copy()
        for f in ["network.ned", f"omnetpp_{t}.ini"]:
            shutil.copy(
                f"data/mkfirm_simulations/{f}",
                f"{D6G_PATH}/simulations/{PACKAGE_NAME}/{f}",
            )


def run_simulation():
    generate_full_omnetini()

    cwd = os.getcwd()
    os.makedirs(f"{D6G_PATH}/simulations/{PACKAGE_NAME}", exist_ok=True)

    os.chdir(f"{D6G_PATH}/simulations/{PACKAGE_NAME}")
    for repetition in range(int(REPETITIONS / SIM_BATCHES)):
        print(
            f"Running Simulations: {repetition * SIM_BATCHES} - {(repetition + 1) * SIM_BATCHES-1}"
        )
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

        os.chdir(cwd)
        for t, r1 in itertools.product(SIMULATIONS, range(SIM_BATCHES)):
            analyze_pcap(t, repetition * SIM_BATCHES + r1)

        os.chdir(f"{D6G_PATH}/simulations/{PACKAGE_NAME}")
        shutil.rmtree("results")

    os.chdir(cwd)


if __name__ == "__main__":
    sys.exit(run_simulation())
