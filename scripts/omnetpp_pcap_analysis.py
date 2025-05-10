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
                        ["", "5G Delay", "E2E Delay", "Max PCP", "Final PCP"]
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


def mkfirm_stream_analysis(topology, stream, test_cases, subfig_ax):
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


def stream_analysis(topology, streams):
    #plt.style.use("data/ieee.mplstyle")

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
        if "mk_firm" in stream and "AGV0_CORE" in stream["name"]:
            subfigs, ax = plt.subplots(1, 2, layout="constrained", figsize=(10, 4))
            mkfirm_stream_analysis(topology, stream, test_cases, ax)
            plt.savefig(
                f"data/mkfirm_simulations/plots/{stream['name']}.png",
                bbox_inches="tight",
            )
            plt.close()


def latest_transmission_start(stream, hop, tsn_config, device_map, link_map):
    hop_name = f"[{device_map[hop[0]]['name']},{device_map[hop[1]]['name']}]"
    exact_transmission = tsn_config["EXACT"][stream["name"]][hop_name]
    gcl = tsn_config["GCL"][hop_name]
    latest_transmission_start = -gcl["offset"]
    for d in gcl["durations"]:
        latest_transmission_start += d
        if latest_transmission_start > exact_transmission:
            latest_transmission_start -= 1e9 * (
                stream["frame_size"] / link_map[f"{hop[0]}-{hop[1]}"]["data_rate"]
            )
            break
    return latest_transmission_start


def qos_violation_analysis():
    plt.style.use("data/ieee.mplstyle")

    topology = parse_json_file("data/emergency_traffic/network.json")
    device_map = {}
    for device in topology["nodes"]:
        device_map[device["id"]] = device
    link_map = {}
    for link in topology["links"]:
        link_map[f"{link['source']}-{link['target']}"] = link

    streams = parse_json_file("data/emergency_traffic/streams.json")

    test_cases = {
        "With ET": {
            "csv_results": "modules/simulation/enabled/csv_results",
            "tsn_config": "data/emergency_traffic/enabled.json",
            "5G_config": "data/emergency_traffic/5G_req_enabled.json",
        },
        "Without ET": {
            "csv_results": "modules/simulation/disabled/csv_results",
            "tsn_config": "data/emergency_traffic/disabled.json",
            "5G_config": "data/emergency_traffic/5G_req_disabled.json",
        },
        "With ET (unrobust)": {
            "csv_results": "modules/simulation/enabled1/csv_results",
            "tsn_config": "data/emergency_traffic/enabled1.json",
            "5G_config": "data/emergency_traffic/5G_req_enabled1.json",
        },
    }

    results = {}

    for test_case, config in test_cases.items():
        tsn_config = parse_json_file(config["tsn_config"])
        pdb_config = parse_json_file(config["5G_config"])

        results[test_case] = {}

        for stream in streams:
            results[test_case][stream["name"]] = {"violations": 0, "count": 0}
            wireless = stream["rti_map"] is not None
            csv_dir = config["csv_results"]
            files = [f for f in os.listdir(csv_dir) if stream["name"] in f]

            # compute expected arrival interval at listener
            talker_offset = latest_transmission_start(
                stream, stream["route"][0], tsn_config, device_map, link_map
            )
            listener = device_map[stream["route"][-1][1]]["name"]
            psfp_entry_n = next(
                e
                for e in tsn_config["PSFP"][listener]
                if stream["name"] in e["streams"]
            )
            e2e = {
                "min": psfp_entry_n["open"] - talker_offset - 1000,
                "max": min(
                    talker_offset + stream["e2e_latency"],
                    psfp_entry_n["open"] - talker_offset + stream["jitter"],
                ),
            }

            for file in files:
                with open(os.path.join(csv_dir, file)) as csv_file:
                    csv_reader = csv.DictReader(csv_file)

                    if wireless:
                        # get 5G packet delay budget
                        pdb = pdb_config[stream["name"]]["pdb"]

                        for row in csv_reader:
                            results[test_case][stream["name"]]["count"] += 1
                            delay_5g = float(row["5G Delay"]) * 1e6
                            delay_e2e = float(row["E2E Delay"]) * 1e6
                            if delay_5g < 0:
                                results[test_case][stream["name"]]["violations"] += 1
                                continue
                            if delay_e2e < e2e["min"] or delay_e2e > e2e["max"]:
                                if pdb["min"] <= delay_5g and delay_5g <= pdb["max"]:
                                    results[test_case][stream["name"]][
                                        "violations"
                                    ] += 1
                    else:
                        for row in csv_reader:
                            results[test_case][stream["name"]]["count"] += 1
                            delay_e2e = float(row["E2E Delay"]) * 1e6
                            if delay_e2e < e2e["min"] or delay_e2e > e2e["max"]:
                                results[test_case][stream["name"]]["violations"] += 1

            results[test_case][stream["name"]] = (
                results[test_case][stream["name"]]["violations"]
                / results[test_case][stream["name"]]["count"]
            )

    for test_case in test_cases:
        worst_impairment = max(results[test_case], key=results[test_case].get)
        print(
            f"{test_case}: {worst_impairment} {results[test_case][worst_impairment]} "
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
        "latency", help="Analyze single stream from csv files"
    )
    subparser2.add_argument("-t", "--topology_input", default="data/network.json")
    subparser2.add_argument("-s", "--streams_input", default="data/streams.json")

    subparsers.add_parser(
        "violations",
        help="Analyze total and conditional end-to-end QoS violations (conditional excludes cases where the 5GS already violates its PDB).",
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
    elif args.subroutine == "latency":
        topology = parse_json_file(args.topology_input)
        streams = parse_json_file(args.streams_input)
        stream_analysis(topology, streams)
    elif args.subroutine == "violations":
        qos_violation_analysis()


if __name__ == "__main__":
    main()
