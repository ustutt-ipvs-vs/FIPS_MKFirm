import argparse
import math
import random
import itertools
import json
import subprocess
import sys

RTI_POLICIES = {
    "minimize_interval": 0,  # more scheduling flexibility
    "minimize_dmax": 1,  # better suited for emergency traffic
}  # chose "minimize_dmax" iff using emergency traffic

STREAM_OBJECTIVES = {
    "no_objective": 0,
    "lateness": 1,
    "tardiness": 2,
    "jitter": 3,
    "tardiness_and_jitter": 4,
}

DATA_RATE = 100000000  # 100Mbps
PROPAGATION_DELAY = 50  # 50ns (~10m Ethernet cable)
PROCESSING_DELAY = 0

CT_TYPES = 1
CT_PERIOD = [5000000]
CT_PHASE = [0]
CT_FRAMESIZE = [100]
CT_E2E_LATENCY = [500000]
CT_JITTER = [0]
CT_PCP = [6]

WT_TYPES = 4
WT_RANDOM_WEIGHTS = [0, 1, 1, 1]
WT_PERIOD = [20000000, 20000000, 20000000, 20000000]
WT_PHASE = [0, 0, 0, 0]
WT_FRAMESIZE = [100, 100, 100, 100]
WT_STABLE_LATENCY = [20000000, 20000000, 20000000, 20000000]
WT_STABLE_JITTER = [0, 0, 0, 0]
WT_MK_FIRM_MASK = ["000", "110", "101", "011"]
WT_MK_FIRM_LATENCY = [20000000, 20000000, 20000000, 20000000]
WT_PCP = [5, 5, 5, 5]

WT_RELIABILITY = [0.5, 0.5, 0.5, 0.5]
WT_RTI_POLICY = "minimize_dmax"
STREAM_OBJECTIVE = "tardiness"

OMNETPP_X = 700
OMNETPP_Y = 500

offset = 0
network = {"nodes": [], "links": []}
streams = []

device_type = {
    "END_DEVICE": 0,
    "TSN_BRIDGE": 1,
    "DS_TT": 2,
    "NW_TT": 3,
    "UNSPECIFIED": 4,
}


def random_path(size: int, offset: int):
    talker = random.randrange(2 ** (size - 1) - 1, 2**size - 1)
    path = [talker]
    while path[-1] != 0:
        path.append(math.floor((path[-1] - 1) / 2))

    path = [i + offset for i in path]
    return path


def build_core(size: int, ct: int, bypass: bool):
    global offset

    for node in range(0, 2**size - 1):
        xpos = 0
        ypos = 0
        node_type = "UNSPECIFIED"
        if node == 0:
            xpos = OMNETPP_X
            ypos = OMNETPP_Y
            node_type = "NW_TT"
        elif node < 2 ** (size - 1) - 1:
            level = math.floor(math.log2(node + 1))
            xpos = OMNETPP_X + 100 * level
            ypos = int(
                OMNETPP_Y
                + 100
                * 2 ** (size - 1 - level)
                * (node - 2**level - 2 ** (level - 1) + 1.5)
            )
            node_type = "TSN_BRIDGE"
        else:
            level = math.floor(math.log2(node + 1))
            xpos = OMNETPP_X + 100 * level
            ypos = int(OMNETPP_Y + 100 * (node - 2**level - 2 ** (level - 1) + 1.5))
            node_type = "END_DEVICE"

        # device
        network["nodes"].append(
            {
                "id": offset + node,
                "name": f"CORE_{str(node).zfill(2)}",
                "processing_delay": PROCESSING_DELAY,
                "type": device_type[node_type],
                "position": {"x": xpos, "y": ypos},
            }
        )

        # links
        if node > 0:
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "source": offset + node,
                    "target": offset + math.floor((node - 1) / 2),
                }
            )
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "target": offset + node,
                    "source": offset + math.floor((node - 1) / 2),
                }
            )
        if node == 2:
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "target": offset + 1,
                    "source": offset + 2,
                }
            )
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "target": offset + 2,
                    "source": offset + 1,
                }
            )

    # cross traffic
    for s in range(0, ct):
        path_a = (
            random_path(size, offset) if not bypass else random_path(size, offset)[:-1]
        )
        path_b = []
        while len(path_b) == 0:
            path_b = (
                random_path(size, offset)
                if not bypass
                else random_path(size, offset)[:-1]
            )
            path_b = [x for x in path_b if x not in path_a]
        path_a = path_a[: len(path_b) + 1]
        path = path_a + list(reversed(path_b))
        route = list(itertools.pairwise(path))

        i = random.randrange(CT_TYPES)
        streams.append(
            {
                "name": f"CORE_CT{str(s).zfill(2)}",
                "period": CT_PERIOD[i],
                "phase": CT_PHASE[i],
                "pcp": CT_PCP[i],
                "stable_qos": {
                    "objective_type": STREAM_OBJECTIVES[STREAM_OBJECTIVE],
                    "latency": CT_E2E_LATENCY[i],
                    "jitter": CT_JITTER[i],
                },
                "frame_size": CT_FRAMESIZE[i],
                "route": route,
                "pdb_map": None,
                "weight": 1.0,
            }
        )

    offset += 2**size - 1


def write_histograms(pdc, vslot):
    if pdc < 0:
        return

    name = str(pdc * 100).replace(".", "-")

    subprocess.run(
        [
            "python",
            "scripts/histogram_manipulation.py",
            "-hist",
            "data/histograms/uplink_histogram.json",
            "-pdc",
            str(pdc),
            "-vslot",
            str(vslot),
            "-name",
            f"pdc_uplink_{name}_{vslot}",
            "-save",
        ]
    )
    subprocess.run(
        [
            "python",
            "scripts/histogram_manipulation.py",
            "-hist",
            "data/histograms/downlink_histogram.json",
            "-pdc",
            str(pdc),
            "-vslot",
            str(vslot),
            "-name",
            f"pdc_downlink_{name}_{vslot}",
            "-save",
        ]
    )


def build_agv(
    n: int,
    size: int,
    csize: int,
    wt_in: int,
    wt_out: int,
    ct: int,
    rel: list[float],
    jitter: list[float],
    bypass: bool,
    pdc: float,
    vslot: int,
):
    global offset, OMNETPP_Y

    pdc_name = str(pdc * 100).replace(".", "-")

    for node in range(0, 2**size - 1):
        xpos = 0
        ypos = 0
        node_type = "UNSPECIFIED"
        if node == 0:
            xpos = OMNETPP_X - 200
            ypos = OMNETPP_Y
            node_type = "DS_TT"
        elif node < 2 ** (size - 1) - 1:
            level = math.floor(math.log2(node + 1))
            xpos = OMNETPP_X - 100 * (level + 2)
            ypos = int(
                OMNETPP_Y
                + 100
                * 2 ** (size - 1 - level)
                * (node - 2**level - 2 ** (level - 1) + 1.5)
            )
            node_type = "TSN_BRIDGE"
        else:
            level = math.floor(math.log2(node + 1))
            xpos = OMNETPP_X - 100 * (level + 2)
            ypos = int(OMNETPP_Y + 100 * (node - 2**level - 2 ** (level - 1) + 1.5))
            node_type = "END_DEVICE"

        # device
        network["nodes"].append(
            {
                "id": offset + node,
                "name": f"AGV{n}_{str(node).zfill(2)}",
                "processing_delay": PROCESSING_DELAY,
                "type": device_type[node_type],
                "position": {"x": xpos, "y": ypos},
            }
        )

        # links
        if node > 0:
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "source": offset + node,
                    "target": offset + math.floor((node - 1) / 2),
                }
            )
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "target": offset + node,
                    "source": offset + math.floor((node - 1) / 2),
                }
            )
        if node == 2:
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "target": offset + 1,
                    "source": offset + 2,
                }
            )
            network["links"].append(
                {
                    "type": 0,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "target": offset + 2,
                    "source": offset + 1,
                }
            )
        if node == 0:
            network["links"].append(
                {
                    "type": 1,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "multiple_subcarriers": True,
                    "target": offset,
                    "source": 0,
                }
            )
            network["links"].append(
                {
                    "type": 1,
                    "data_rate": DATA_RATE,
                    "propagation_delay": PROPAGATION_DELAY,
                    "multiple_subcarriers": True,
                    "target": 0,
                    "source": offset,
                }
            )

    # cross traffic
    for s in range(0, ct):
        path_a = (
            random_path(size, offset) if not bypass else random_path(size, offset)[:-1]
        )
        path_b = []
        while len(path_b) == 0:
            path_b = (
                random_path(size, offset)
                if not bypass
                else random_path(size, offset)[:-1]
            )
            path_b = [x for x in path_b if x not in path_a]
        path_a = path_a[: len(path_b) + 1]
        path = path_a + list(reversed(path_b))
        route = list(itertools.pairwise(path))

        i = random.randrange(CT_TYPES)
        streams.append(
            {
                "name": f"AGV{n}_CT{str(s).zfill(2)}",
                "period": CT_PERIOD[i],
                "phase": CT_PHASE[i],
                "pcp": CT_PCP[i],
                "stable_qos": {
                    "objective_type": STREAM_OBJECTIVES[STREAM_OBJECTIVE],
                    "latency": CT_E2E_LATENCY[i],
                    "jitter": CT_JITTER[i],
                },
                "frame_size": CT_FRAMESIZE[i],
                "route": route,
                "pdb_map": None,
                "weight": 1.0,
            }
        )

    # incoming wireless traffic
    for s in range(0, wt_in):
        route = (
            list(itertools.pairwise(random_path(csize, 0)))
            + [(0, offset)]
            + list(itertools.pairwise(list(reversed(random_path(size, offset)))))
        )

        i = random.choices(range(WT_TYPES), weights=WT_RANDOM_WEIGHTS)[0]
        if pdc > 0:
            streams.append(
                {
                    "name": f"CORE_AGV{n}_{str(s).zfill(2)}",
                    "period": WT_PERIOD[i],
                    "phase": WT_PHASE[i],
                    "pcp": WT_PCP[i],
                    "mk_firm": {
                        "mask": WT_MK_FIRM_MASK[i],
                        "latency": WT_MK_FIRM_LATENCY[i],
                    },
                    "stable_qos": {
                        "objective_type": STREAM_OBJECTIVES[STREAM_OBJECTIVE],
                        "latency": WT_STABLE_LATENCY[i],
                        "jitter": jitter[i],
                    },
                    "frame_size": WT_FRAMESIZE[i],
                    "route": route,
                    "pdb_map": [
                        {
                            "link": (0, offset),
                            "reliability": rel[i],
                            "policy": RTI_POLICIES[WT_RTI_POLICY],
                            "histogram": f"../data/modified_histograms/pdc_downlink_{pdc_name}_{vslot}.json",
                        }
                    ],
                    "weight": 1.0,
                }
            )
        else:
            streams.append(
                {
                    "name": f"CORE_AGV{n}_{str(s).zfill(2)}",
                    "period": WT_PERIOD[i],
                    "phase": WT_PHASE[i],
                    "pcp": WT_PCP[i],
                    "mk_firm": {
                        "mask": WT_MK_FIRM_MASK[i],
                        "latency": WT_MK_FIRM_LATENCY[i],
                    },
                    "stable_qos": {
                        "objective_type": STREAM_OBJECTIVES[STREAM_OBJECTIVE],
                        "latency": WT_STABLE_LATENCY[i],
                        "jitter": jitter[i],
                    },
                    "frame_size": WT_FRAMESIZE[i],
                    "route": route,
                    "pdb_map": [
                        {
                            "link": (0, offset),
                            "reliability": rel[i],
                            "policy": RTI_POLICIES[WT_RTI_POLICY],
                            "histogram": "../data/histograms/downlink_histogram.json",
                        }
                    ],
                    "weight": 1.0,
                }
            )

    # outgoing wireless traffic
    for s in range(0, wt_out):
        route = (
            list(itertools.pairwise(random_path(size, offset)))
            + [(offset, 0)]
            + list(itertools.pairwise(list(reversed(random_path(csize, 0)))))
        )

        i = random.choices(range(WT_TYPES), weights=WT_RANDOM_WEIGHTS)[0]
        if pdc > 0:
            streams.append(
                {
                    "name": f"AGV{n}_CORE_{str(s).zfill(2)}",
                    "period": WT_PERIOD[i],
                    "phase": WT_PHASE[i],
                    "pcp": WT_PCP[i],
                    "mk_firm": {
                        "mask": WT_MK_FIRM_MASK[i],
                        "latency": WT_MK_FIRM_LATENCY[i],
                    },
                    "stable_qos": {
                        "objective_type": STREAM_OBJECTIVES[STREAM_OBJECTIVE],
                        "latency": WT_STABLE_LATENCY[i],
                        "jitter": jitter[i],
                    },
                    "frame_size": WT_FRAMESIZE[i],
                    "route": route,
                    "pdb_map": [
                        {
                            "link": (offset, 0),
                            "reliability": rel[i],
                            "policy": RTI_POLICIES[WT_RTI_POLICY],
                            "histogram": f"../data/modified_histograms/pdc_uplink_{pdc_name}_{vslot}.json",
                        }
                    ],
                    "weight": 1.0,
                }
            )
        else:
            streams.append(
                {
                    "name": f"AGV{n}_CORE_{str(s).zfill(2)}",
                    "period": WT_PERIOD[i],
                    "phase": WT_PHASE[i],
                    "pcp": WT_PCP[i],
                    "mk_firm": {
                        "mask": WT_MK_FIRM_MASK[i],
                        "latency": WT_MK_FIRM_LATENCY[i],
                    },
                    "stable_qos": {
                        "objective_type": STREAM_OBJECTIVES[STREAM_OBJECTIVE],
                        "latency": WT_STABLE_LATENCY[i],
                        "jitter": jitter[i],
                    },
                    "frame_size": WT_FRAMESIZE[i],
                    "route": route,
                    "pdb_map": [
                        {
                            "link": (offset, 0),
                            "reliability": rel[i],
                            "policy": RTI_POLICIES[WT_RTI_POLICY],
                            "histogram": "../data/histograms/uplink_histogram.json",
                        }
                    ],
                    "weight": 1.0,
                }
            )

    offset += 2**size - 1
    OMNETPP_Y += 100 * 2 ** (size - 1)


def main(raw_args=None):
    global offset, network, streams

    offset = 0
    network = {"nodes": [], "links": []}
    streams = []

    parser = argparse.ArgumentParser(
        prog="Robost Network Builder",
        description="builds a simple network with N AGVs that want to communication with M edge servers",
    )

    parser.add_argument(
        "-no_bypass", "--cross_traffic_bypass", default=True, action="store_false"
    )

    parser.add_argument(
        "-agvs", "--agv_size_specification", type=int, nargs="+", default=[3]
    )
    parser.add_argument(
        "-agv_wt_out",
        "--agv_outgoing_wireless_traffic_specification",
        type=int,
        nargs="+",
        default=[30],
    )
    parser.add_argument(
        "-agv_wt_in",
        "--agv_ingoing_wireless_traffic_specification",
        type=int,
        nargs="+",
        default=[30],
    )
    parser.add_argument(
        "-agv_ct", "--agv_cross_traffic_specification", type=int, nargs="+", default=[5]
    )

    parser.add_argument("-core", "--core_size_specification", type=int, default=4)
    parser.add_argument(
        "-core_ct", "--core_cross_traffic_specification", type=int, default=5
    )

    parser.add_argument(
        "-rel", "--reliability", type=float, nargs="+", default=WT_RELIABILITY
    )
    parser.add_argument(
        "-jitter", "--jitter", type=float, nargs="+", default=WT_STABLE_JITTER
    )

    parser.add_argument("-pdc", "--packet_delay_correction", type=float, default=0)
    parser.add_argument("-vslot", "--virtual_slot_size", type=int, default=0)
    parser.add_argument("-suffix", "--suffix", type=str, default="")
    parser.add_argument("-prefix", "--prefix", type=str, default="data")

    parser.add_argument("-seed", "--seed", type=int, default=None)
    parser.add_argument("-q", "--quiet", default=False, action="store_true")

    args = parser.parse_args(raw_args)
    random.seed(args.seed)
    path_suffix = args.suffix
    path_prefix = args.prefix

    build_core(
        args.core_size_specification,
        args.core_cross_traffic_specification,
        args.cross_traffic_bypass,
    )
    for i in range(len(args.agv_size_specification)):
        build_agv(
            i,
            args.agv_size_specification[i],
            args.core_size_specification,
            args.agv_ingoing_wireless_traffic_specification[i],
            args.agv_outgoing_wireless_traffic_specification[i],
            args.agv_cross_traffic_specification[i],
            args.reliability,
            args.jitter,
            args.cross_traffic_bypass,
            args.packet_delay_correction,
            args.virtual_slot_size,
        )

    if not args.quiet:
        print(f"generated network with {len(streams)} streams")

    with open(f"{path_prefix}/network{path_suffix}.json", "w") as f:
        json.dump(network, f, indent=4)
    with open(f"{path_prefix}/streams{path_suffix}.json", "w") as f:
        json.dump(streams, f, indent=4)
    if args.packet_delay_correction > 0:
        write_histograms(args.packet_delay_correction, args.virtual_slot_size)

    return 0


if __name__ == "__main__":
    sys.exit(main())
