from agv_network_builder import main as benchmark_builder
from omnetpp_generator import main as omnetpp_generator, STREAM_TO_MODULE_MAP
from omnetpp_pcap_analysis import main as pcap_analysis
import agv_network_builder as agv
import subprocess
import shutil
import os
import json
import itertools
import math
import sys
import pandas as pd
from omnetpp.scave import results
from multiprocessing import Process

# Benchmark Parameters
AGV_WT_OUT = 40
AGV_WT_IN = 40
AGV_CT = 10
CORE_CT = 10

WT_RELIABILITY = 0.999
TALKER_RELIABILITY = 0.999

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

agv.WT_TYPES = 4
agv.WT_RANDOM_WEIGHTS = [0, 1, 1, 1]
agv.WT_PERIOD = [20000000] * agv.WT_TYPES
agv.WT_PHASE = [0] * agv.WT_TYPES
agv.WT_FRAMESIZE = [100] * agv.WT_TYPES
agv.WT_STABLE_LATENCY = [20000000] * agv.WT_TYPES
agv.WT_STABLE_JITTER = [0] * agv.WT_TYPES
agv.WT_MK_FIRM_MASK = ["000", "110", "101", "011"]
agv.WT_MK_FIRM_LATENCY = [20000000] * agv.WT_TYPES
agv.WT_PCP = [5] * agv.WT_TYPES

agv.WT_RELIABILITY = [WT_RELIABILITY] * agv.WT_TYPES
agv.WT_RTI_POLICY = "minimize_dmax"
agv.STREAM_OBJECTIVE = "tardiness"

REPETITIONS = 1000
SIM_TIME = 20  # 20s = 1000 hypercycles
SIM_BATCHES = 25  # run X repetitions of each simulation in parallel

PACKAGE_NAME = "skip_factor"
D6G_PATH = "/usr/src/omnetpp/workspace/deterministic6g"
INET_PATH = "/usr/src/omnetpp/workspace/inet"

SIMULATIONS = ["skipfactor_configuration", "mkfirm_configuration"]
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
        "data/skipfactor_simulations",
        "-q",
    ]
    benchmark_builder(args)


def analyze_pcap(t, run=0):
    os.makedirs("data/skipfactor_simulations/csv", exist_ok=True)
    os.makedirs(f"data/skipfactor_simulations/csv/{t}", exist_ok=True)

    args = [
        "pcap",
        "-t",
        "data/skipfactor_simulations/network.json",
        "-s",
        "data/skipfactor_simulations/streams.json",
        "-in",
        f"{D6G_PATH}/simulations/skip_factor/results/{t}",
        "-out",
        f"data/skipfactor_simulations/csv/{t}",
        "--suffix",
        str(run),
    ]
    pcap_analysis(args)


def generate_omnetini(t=""):
    args = [
        "-t",
        "data/skipfactor_simulations/network.json",
        "-s",
        "data/skipfactor_simulations/streams.json",
        "-g",
        f"data/skipfactor_simulations/{t}.json",
        "-ned",
        "data/skipfactor_simulations/network.ned",
        "-ini",
        f"data/skipfactor_simulations/omnetpp_{t}.ini",
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
        "--talker_delay",
        f"bernoulli({TALKER_RELIABILITY}) == 1 ? 0s : 1ms",
    ]
    if t == "skipfactor_configuration":
        args.append("--skip_factor")

    omnetpp_generator(args)


def generate_full_omnetini():
    os.makedirs("data/skipfactor_simulations", exist_ok=True)

    build_benchmark()
    cwd = os.getcwd()
    os.chdir("release")
    handle = subprocess.Popen(
        [
            "./benchmarks/mk_firm",
            "-n",
            "../data/skipfactor_simulations/network.json",
            "-s",
            "../data/skipfactor_simulations/streams.json",
            "--output_normal",
            "../data/skipfactor_simulations/skipfactor_configuration.json",
            "--output_mkfirm",
            "../data/skipfactor_simulations/mkfirm_configuration.json",
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
                f"data/skipfactor_simulations/{f}",
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

        print(" -> evaluating results")
        processes = []
        for t, r1 in itertools.product(SIMULATIONS, range(SIM_BATCHES)):
            p = Process(
                target=analyze_pcap,
                args=(
                    t,
                    repetition * SIM_BATCHES + r1,
                ),
            )
            p.start()
            processes.append(p)
        for p in processes:
            p.join()

        os.chdir(f"{D6G_PATH}/simulations/{PACKAGE_NAME}")
        shutil.rmtree("results")

    os.chdir(cwd)


if __name__ == "__main__":
    sys.exit(run_simulation())
