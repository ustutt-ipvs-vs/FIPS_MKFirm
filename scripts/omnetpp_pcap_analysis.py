import argparse
import csv
import json
import os
import math
from scapy.utils import PcapReader
from scapy.all import UDP, Dot1Q, Raw
import matplotlib.pyplot as plt
import numpy as np

PORT = 2000


def parse_json_file(filename):
    with open(filename) as f:
        return json.load(f)


def parse_pcap(topology, streams, pcap_dir, csv_dir, suffix):
    results = {}

    device_map = {}
    for device in topology["nodes"]:
        device_map[device["id"]] = device

    link_map = {}
    for link in topology["links"]:
        link_map[f"{link['source']}-{link['target']}"] = link

    port = PORT
    hyper_period = math.lcm(*[stream["period"] for stream in streams]) / 1e6
    for stream in streams:
        for frame in range(int(1e6 * hyper_period / stream["period"])):
            results[port] = {
                "frame": f"{stream['name']}-{frame}",
                "period": stream["period"],
                "listener": device_map[stream["route"][-1][1]]["name"],
                "max_pcp": [],
                "final_pcp": [],
                "transmissions": {},
                "arrivals": {},
            }
            for hop in stream["route"]:
                results[port]["transmissions"][device_map[hop[0]]["name"]] = []
                results[port]["arrivals"][device_map[hop[1]]["name"]] = []
            port += 1

    for file in os.listdir(pcap_dir):
        if not file.endswith(f"{suffix}.pcap"):
            continue
        device = file.split("-")[0]

        pcap = PcapReader(os.path.join(pcap_dir, file))
        for pkt in pcap:
            stream_results = results[pkt[UDP].dport]

            seqnr = int.from_bytes(bytes(Raw(pkt[UDP].payload).load)[4:8], "big")
            if pkt.direction == 1:
                i = len(stream_results["arrivals"][device])
                if seqnr >= i:
                    stream_results["arrivals"][device] += [-1] * (seqnr - i + 1)
                offset = stream_results["period"] / 1e9 * seqnr
                stream_results["arrivals"][device][seqnr] = 1e3 * (
                    float(pkt.time) - offset
                )

                if device == stream_results["listener"]:
                    if seqnr >= i:
                        stream_results["final_pcp"] += [-1] * (seqnr - i + 1)
                    pcp = int(pkt[Dot1Q].prio)
                    stream_results["final_pcp"][seqnr] = pcp

            elif pkt.direction == 2:
                i = len(stream_results["transmissions"][device])
                if seqnr >= i:
                    stream_results["transmissions"][device] += [-1] * (seqnr - i + 1)
                offset = stream_results["period"] / 1e9 * seqnr
                stream_results["transmissions"][device][seqnr] = 1e3 * (
                    float(pkt.time) - offset
                )

                pcp = int(pkt[Dot1Q].prio)
                i = len(stream_results["max_pcp"])
                if seqnr >= i:
                    stream_results["max_pcp"] += [-1] * (seqnr - i + 1)
                stream_results["max_pcp"][seqnr] = max(
                    pcp, stream_results["max_pcp"][seqnr]
                )

        pcap.close()

    port = PORT
    for stream in streams:
        for frame in range(int(1e6 * hyper_period / stream["period"])):
            stream_res = results[port]

            if "mk_firm" in stream:
                wireless_hop = next(
                    h
                    for h in stream["route"]
                    if link_map[f"{h[0]}-{h[1]}"]["type"] == 1
                )

                talker = device_map[stream["route"][0][0]]["name"]
                tt1 = device_map[wireless_hop[0]]["name"]
                tt2 = device_map[wireless_hop[1]]["name"]
                listener = device_map[stream["route"][-1][1]]["name"]
                N = len(stream_res["arrivals"][listener])

                with open(
                    os.path.join(csv_dir, f"{stream_res['frame']}{suffix}.csv"), "w"
                ) as f:
                    csv_writer = csv.writer(f, delimiter=",")
                    csv_writer.writerow(
                        [
                            "",
                            "5G Delay",
                            "TT Arrival",
                            "E2E Delay",
                            "Max PCP",
                            "Final PCP",
                        ]
                    )

                    for j in range(N):
                        wireless_delay = (
                            stream_res["arrivals"][tt2][j]
                            - stream_res["transmissions"][tt1][j]
                        )
                        ete_delay = (
                            stream_res["arrivals"][listener][j]
                            - stream_res["transmissions"][talker][j]
                        )

                        csv_writer.writerow(
                            [
                                j,
                                wireless_delay if wireless_delay > 0 else -1,
                                (
                                    stream_res["arrivals"][tt2][j]
                                    if wireless_delay > 0
                                    else -1
                                ),
                                ete_delay if ete_delay > 0 else -1,
                                stream_res["max_pcp"][j],
                                stream_res["final_pcp"][j],
                            ]
                        )
            else:
                talker = device_map[stream["route"][0][0]]["name"]
                listener = device_map[stream["route"][-1][1]]["name"]
                N = len(stream_res["arrivals"][listener])

                with open(
                    os.path.join(csv_dir, f"{stream_res['frame']}{suffix}.csv"), "w"
                ) as f:
                    csv_writer = csv.writer(f, delimiter=",")
                    csv_writer.writerow(["", "E2E Delay"])

                    for j in range(N):
                        ete_delay = (
                            stream_res["arrivals"][listener][j]
                            - stream_res["transmissions"][talker][j]
                        )

                        csv_writer.writerow([j, ete_delay if ete_delay > 0 else -1])

            port += 1


def wireless_stream_analysis(topology, stream, test_cases, subfig_ax):
    XMIN = 0
    XMAX = 31
    XSTEP = 0.1

    k = len(stream["mk_firm"]["mask"])

    for test_case, config in test_cases.items():
        csv_dir = config["csv_results"]
        files = [f for f in os.listdir(csv_dir) if stream["name"] in f]

        xvalues = []
        yvalues = {"normal": [], "elevated": []}

        reduced_x = np.arange(XMIN, XMAX, XSTEP)
        reduced_y = {
            "normal": [[] for _ in reduced_x],
            "elevated": [[] for _ in reduced_x],
        }
        faults = []
        faulty = False

        for file in files:
            with open(os.path.join(csv_dir, file)) as csv_file:
                csv_reader = csv.DictReader(csv_file)
                for row in csv_reader:
                    if float(row["5G Delay"]) < 0:
                        continue
                    i = int(row[""])
                    xvalues.append(float(row["5G Delay"]))
                    if 1e6 * xvalues[-1] > stream["period"]:
                        faulty = True
                    x = round((xvalues[-1] - XMIN) / XSTEP)
                    if stream["mk_firm"]["mask"][i % k] == "1":
                        if float(row["E2E Delay"]) < 0 and test_case == "MKFirm":
                            faults.append(f"{file} {row}")
                        yvalues["elevated"].append(float(row["E2E Delay"]))
                        reduced_y["elevated"][x].append(yvalues["elevated"][-1])
                    else:
                        yvalues["normal"].append(float(row["E2E Delay"]))
                        reduced_y["normal"][x].append(yvalues["normal"][-1])

        if not faulty:
            for error in faults:
                print(error)

        os.makedirs(config["output"], exist_ok=True)

        for t in ["normal", "elevated"]:
            plt_x = []
            plt_y = []
            with open(
                os.path.join(config["output"], f"{stream['name']}_{t}.csv"), "w"
            ) as csvfile:
                fieldnames = ["x", "y"]
                writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                writer.writeheader()

                for i, x in enumerate(reduced_x):
                    if not reduced_y[t][i]:
                        continue
                    reduced_y[t][i].sort()
                    yvals = [
                        min(reduced_y[t][i]),
                        reduced_y[t][i][int(len(reduced_y[t][i]) / 2)],
                        max(reduced_y[t][i]),
                    ]
                    for y in yvals:
                        writer.writerow({"x": x, "y": y})
                        plt_x.append(x)
                        plt_y.append(y)

            subfig_ax[config["subfigure"]].scatter(plt_x, plt_y, s=1, label=t)
            subfig_ax[config["subfigure"]].set_xlabel("5G Port-to-Port Delay [ms]")
            subfig_ax[config["subfigure"]].set_ylabel("E2E Delay [ms]")


def wired_stream_analysis(topology, stream, test_cases):
    for test_case, config in test_cases.items():
        csv_dir = config["csv_results"]
        files = [f for f in os.listdir(csv_dir) if stream["name"] in f]

        y_values = []
        for file in files:
            with open(os.path.join(csv_dir, file)) as csv_file:
                csv_reader = csv.DictReader(csv_file)
                for row in csv_reader:
                    y_values.append(float(row["E2E Delay"]))

        os.makedirs(config["output"], exist_ok=True)
        with open(
            os.path.join(config["output"], f"{stream['name']}.csv"), "w"
        ) as csvfile:
            fieldnames = ["min", "med", "max"]
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            reduced_y = {
                "min": min(y_values),
                "med": y_values[int(len(y_values) / 2)],
                "max": max(y_values),
            }
            writer.writerow(reduced_y)

            if (
                reduced_y["min"] < 0
                or reduced_y["max"] > stream["stable_qos"]["latency"]
            ):
                print(f"ERROR: Violation detected for {stream['name']}")


def stream_analysis(topology, streams):
    # plt.style.use("data/ieee.mplstyle")

    test_cases = {
        "MKFirm": {
            "csv_results": "data/mkfirm_simulations/csv/mkfirm_configuration",
            "output": "data/mkfirm_simulations/csv/mkfirm_configuration_reduced",
            "subfigure": 0,
        },
        "Normal": {
            "csv_results": "data/mkfirm_simulations/csv/tsn_configuration",
            "output": "data/mkfirm_simulations/csv/tsn_configuration_reduced",
            "subfigure": 1,
        },
    }

    os.makedirs("data/mkfirm_simulations/plots", exist_ok=True)

    for stream in streams:
        print(stream["name"])
        if "mk_firm" in stream:
            subfigs, ax = plt.subplots(1, 2, layout="constrained", figsize=(10, 4))
            wireless_stream_analysis(topology, stream, test_cases, ax)
            plt.savefig(
                f"data/mkfirm_simulations/plots/{stream['name']}.png",
                bbox_inches="tight",
            )
            plt.close()
        else:
            wired_stream_analysis(topology, stream, test_cases)


def stream_reliability(topology, stream, path):
    files = [f for f in os.listdir(path) if stream["name"] in f]

    count = [0, 0]
    for file in files:
        with open(os.path.join(path, file)) as csv_file:
            csv_reader = csv.DictReader(csv_file)
            for row in csv_reader:
                if (
                    float(row["E2E Delay"]) >= 0
                    and float(row["E2E Delay"]) <= stream["stable_qos"]["latency"] / 1e6
                ):
                    count[0] += 1
                count[1] += 1

    return 100 * (count[0] / count[1])


def stream_whrt_violations(topology, stream, path):
    files = [f for f in os.listdir(path) if stream["name"] in f]

    m = stream["mk_firm"]["mask"].count("1")
    k = len(stream["mk_firm"]["mask"])

    sliding_window = []
    count = 0
    elevation_count = 0

    for file in files:
        with open(os.path.join(path, file)) as csv_file:
            csv_reader = csv.DictReader(csv_file)
            for row in csv_reader:
                violation = (
                    float(row["E2E Delay"]) < 0
                    or float(row["E2E Delay"]) > stream["mk_firm"]["latency"] / 1e6
                )
                sliding_window.append(violation)
                if len(sliding_window) > k:
                    sliding_window.pop(0)

                if len(sliding_window) == k and sliding_window.count(False) < m:
                    count += 1

                if int(row["Max PCP"]) == 7:
                    elevation_count += 1

    return count, elevation_count


def reliability(topology, streams, path):
    directories = [f.path for f in os.scandir(path) if f.is_dir()]

    print(
        "Disclaimer: WHRT violations are unconditional (i.e., the printed number contains also occurances where the 5G delay would already exceed the E2E latency requirement)"
    )

    for directory in directories:
        print(directory)
        with open(os.path.join(directory, f"summary.csv"), "w") as csv_file:
            writer = csv.DictWriter(
                csv_file,
                fieldnames=[
                    "x",
                    "stream",
                    "reliability",
                    "(m,k)-firm violations",
                    "elevations",
                ],
            )
            writer.writeheader()

            for x, stream in enumerate(streams):
                rel = stream_reliability(topology, stream, directory)
                if "mk_firm" in stream:
                    violations, elevations = stream_whrt_violations(
                        topology, stream, directory
                    )
                    print(stream["name"], rel, violations, elevations)
                    writer.writerow(
                        {
                            "x": x,
                            "stream": stream["name"],
                            "reliability": rel,
                            "(m,k)-firm violations": violations,
                            "elevations": elevations,
                        }
                    )
                else:
                    print(stream["name"], rel)
                    writer.writerow(
                        {
                            "x": x,
                            "stream": stream["name"],
                            "reliability": rel,
                        }
                    )


def main(raw_args=None):
    parser = argparse.ArgumentParser(
        prog="python scripts/omnetpp_pcap_analysis.py",
        description="",
    )
    subparsers = parser.add_subparsers(dest="subroutine")

    subparser1 = subparsers.add_parser("pcap", help="Convert pcap into csv files")
    subparser1.add_argument("-t", "--topology_input", default="data/network.json")
    subparser1.add_argument("-s", "--streams_input", default="data/streams.json")
    subparser1.add_argument(
        "-in", "--pcap_input_directory", default="modules/simulation/enabled/results"
    )
    subparser1.add_argument(
        "-out",
        "--csv_output_directory",
        default="modules/simulation/enabled/csv_results",
    )
    subparser1.add_argument(
        "--suffix",
        default="",
    )

    subparser2 = subparsers.add_parser(
        "analyze", help="Analyze single stream from csv files"
    )
    subparser2.add_argument(
        "-t", "--topology_input", default="data/mkfirm_simulations/network.json"
    )
    subparser2.add_argument(
        "-s", "--streams_input", default="data/mkfirm_simulations/streams.json"
    )

    subparser3 = subparsers.add_parser(
        "reliability",
        help="Analyze total and conditional end-to-end QoS violations (conditional excludes cases where the 5GS already violates its PDB).",
    )
    subparser3.add_argument(
        "-t", "--topology_input", default="data/skipfactor_simulations/network.json"
    )
    subparser3.add_argument(
        "-s", "--streams_input", default="data/skipfactor_simulations/streams.json"
    )
    subparser3.add_argument(
        "-d", "--directory", default="data/skipfactor_simulations/csv"
    )

    args = parser.parse_args(raw_args)
    if args.subroutine is None:
        parser.print_help()
        return

    if args.subroutine == "pcap":
        topology = parse_json_file(args.topology_input)
        streams = parse_json_file(args.streams_input)
        suffix = f"-{args.suffix}" if args.suffix != "" else ""
        parse_pcap(
            topology,
            streams,
            args.pcap_input_directory,
            args.csv_output_directory,
            suffix,
        )
    elif args.subroutine == "analyze":
        topology = parse_json_file(args.topology_input)
        streams = parse_json_file(args.streams_input)
        stream_analysis(topology, streams)
    elif args.subroutine == "reliability":
        topology = parse_json_file(args.topology_input)
        streams = parse_json_file(args.streams_input)
        reliability(topology, streams, args.directory)


if __name__ == "__main__":
    main()
