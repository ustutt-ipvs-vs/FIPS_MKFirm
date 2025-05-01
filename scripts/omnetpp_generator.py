import argparse
import math
import random
import json
from agv_network_builder import device_type


OMNETPP_NED_MIN_POS = 200
OMNETPP_NED_MAX_POS = 1000

PORT = 2000
DETCOM = 0

omnetpp_ned_header = """package d6g.simulations.{package};

import inet.networks.base.TsnNetworkBase;
import inet.node.contract.IEthernetNetworkNode;
import inet.node.ethernet.EthernetLink;
import d6g.devices.DetCom;
import d6g.devices.tsntranslator.TTInterface;
import d6g.devices.tsntranslator.TTChannel;
import d6g.devices.tsntranslator.TsnTranslator;
import d6g.networks.DetComNetworkBase;
import d6g.apps.edgecloud.UdpEdgeCloudBasicApp;
import inet.node.tsn.TsnDevice;
import inet.node.tsn.TsnSwitch;
import ned.DatarateChannel;
import d6g.distribution.histogram.Histogram;
import d6g.distribution.histogram.HistogramContainer;


network {network} extends DetComNetworkBase
{{	
 submodules:
    histogramContainer:HistogramContainer{{
        @display("p=100,800;is=s");
    }}	
"""
tsn_device = """    {name}: TsnDevice {{
            @display("p={x},{y}");  
    }}
"""
tsn_switch = """    {name}: TsnSwitch {{
            @display("p={x},{y}");  
    }}
"""
detcom = """    {name}: DetCom {{
            @display("p={x},{y}");  
    }}
"""
switch_link = "    {source}.ethg++ <--> EthernetLink {{ datarate={datarate}Mbps; delay={delay}ns; }} <--> {target}.ethg++;\n"
dstt_link = "    {source}.ethg++ <--> EthernetLink {{ datarate={datarate}Mbps; delay={delay}ns; }} <--> {target}.dsttg++;\n"
nwtt_link = "    {source}.nwttg <--> EthernetLink {{ datarate={datarate}Mbps; delay={delay}ns; }} <--> {target}.ethg++;\n"


def parse_json_file(filename):
    with open(filename) as f:
        return json.load(f)


def build_network_description_file(topology, ned_output, package, network_name):
    global DETCOM

    ned = omnetpp_ned_header.format(network=network_name, package=package)
    ned_nodes = ""
    device_map = {}
    link_map = {}

    for node in topology["nodes"]:
        if "position" not in node:
            node["position"] = {
                "x": random.randint(OMNETPP_NED_MIN_POS, OMNETPP_NED_MAX_POS),
                "y": random.randint(OMNETPP_NED_MIN_POS, OMNETPP_NED_MAX_POS),
            }
        x = node["position"]["x"]
        y = node["position"]["y"]

        if node["type"] > 0:
            ned_nodes += tsn_switch.format(name=node["name"], x=x, y=y)
        else:
            ned_nodes += tsn_device.format(name=node["name"], x=x, y=y)

        device_map[node["id"]] = node
        device_map[node["id"]]["ifaces"] = []
        device_map[node["id"]]["identifier_entries"] = []
        device_map[node["id"]]["encoder_entries"] = []
        device_map[node["id"]]["app"] = 0
        device_map[node["id"]]["has_outgoing_streams"] = "false"

    ned_links = "\n connections:\n"
    for link in topology["links"]:
        source = device_map[link["source"]]["name"]
        target = device_map[link["target"]]["name"]
        link_map[f"{link['source']}-{link['target']}"] = link

        if link["type"] == 0:
            if link["source"] > link["target"]:
                continue

            ned_links += switch_link.format(
                source=source,
                target=target,
                datarate=link["data_rate"] / 1e6,
                delay=link["propagation_delay"],
            )
        else:
            if device_map[link["source"]]["type"] != device_type["DS_TT"]:
                continue

            ned_nodes += detcom.format(
                name=f"detcom{DETCOM}",
                x=(
                    device_map[link["source"]]["position"]["x"]
                    + device_map[link["target"]]["position"]["x"]
                )
                / 2,
                y=(
                    device_map[link["source"]]["position"]["y"]
                    + device_map[link["target"]]["position"]["y"]
                )
                / 2,
            )

            ned_links += dstt_link.format(
                source=source,
                target=f"detcom{DETCOM}",
                datarate=link["data_rate"] / 1e6,
                delay=link["propagation_delay"],
            )
            ned_links += nwtt_link.format(
                source=f"detcom{DETCOM}",
                target=target,
                datarate=link["data_rate"] / 1e6,
                delay=link["propagation_delay"],
            )
            DETCOM += 1

        device_map[link["source"]]["ifaces"].append(target)
        device_map[link["target"]]["ifaces"].append(source)
    ned += ned_nodes + ned_links + "}"

    with open(ned_output, "w") as f:
        f.write(ned)

    return device_map, link_map


omnetpp_ini = """
{header}
#--------------------------------------------
# Ethernet Link Specification
#--------------------------------------------
{links}

#--------------------------------------------
# End-Station Specification
#--------------------------------------------
{talkers}

#--------------------------------------------
# TT Stream Specification
#--------------------------------------------
{streams}

#--------------------------------------------
# Bridge Specification 
#--------------------------------------------
{bridges}

"""

omnetpp_ini_header = """[{scenario}]
network = d6g.simulations.{package}.{network}
sim-time-limit = {sim_time}s
description = {network}
repeat = {repetitions}

**.crcMode = "computed"
**.fcsMode = "computed"
"""

histograms_ini_header = """
*.histogramContainer.histograms = {{
    Uplink: "{prefix}/uplink.xml", 
    Downlink: "{prefix}/downlink.xml"}}
*.detcom*.**.delayDownlink = rngProvider("histogramContainer","Downlink")
*.detcom*.**.delayUplink = rngProvider("histogramContainer","Uplink")
"""

random_delay_ini_header = """
*.detcom*.**.delayDownlink = uniform(0.1ms, 25ms)
*.detcom*.**.delayUplink = uniform(0.1ms, 25ms)
"""

talker_ini = """
*.{device}.hasOutgoingStreams = {has_outgoing_streams}
*.{device}.numApps = {apps}
*.{device}.numPcapRecorders = 1
*.{device}.pcapRecorder[0].moduleNamePattersn = "eth[*]"
*.{device}.pcapRecorder[0].pcapFile = "results/{device}.pcap"
"""

channel_ini = "*.{source}.eth[{iface}].bitrate = {datarate}Mbps\n"
detcom_channel_ini = """*.detcom{id}.dstt[0].eth[0].bitrate = {datarate}Mbps
*.detcom{id}.nwtt.eth[0].bitrate = {datarate}Mbps
"""

pcp_ini_identifier_entry = (
    '{{stream: "{stream}", packetFilter: expr(udp.destPort == {dest_port})}}'
)
pcp_ini_encoder_entry = '{{stream: "{stream}", pcp: {pcp}}}'
pcp_ini = """
*.{talker}.bridging.streamIdentifier.identifier.mapping = [{identifier_entries}]
*.{talker}.bridging.streamCoder.encoder.mapping = [{encoder_entries}]
"""

tt_stream_ini = """
# Stream {stream}: period {stream_period}ms
*.{talker}.app[{talker_app}].typename = "UdpBasicApp"
*.{talker}.app[{talker_app}].packetName = "{stream}"
*.{talker}.app[{talker_app}].destAddresses = "{listener}"
*.{talker}.app[{talker_app}].destPort = {port}
*.{talker}.app[{talker_app}].messageLength = {frame_size}B - 58B # 58B = 8B (UDP) + 20B (IP) + 14B (ETH MAC) + 4B (Dot1Q) + 4B (ETH FCS) + 8B (ETH PHY)
*.{talker}.app[{talker_app}].sendInterval = {period}ms
*.{talker}.app[{talker_app}].startTime = {offset}ms
*.{listener}.app[{listener_app}].typename = "UdpSinkApp"
*.{listener}.app[{listener_app}].io.localPort = {port}
"""

et_stream_ini = """
*.{talker}.app[{talker_app}].typename = "UdpBasicBurst"
*.{talker}.app[{talker_app}].packetName = "et_{stream}"
*.{talker}.app[{talker_app}].destAddresses = "{listener}"
*.{talker}.app[{talker_app}].chooseDestAddrMode = "once"
*.{talker}.app[{talker_app}].destPort = {port}
*.{talker}.app[{talker_app}].burstDuration = {burst_duration}ms
*.{talker}.app[{talker_app}].startTime = {interevent_time}ms + exponential({exp_param}ms)
*.{talker}.app[{talker_app}].sleepDuration = {interevent_time}ms + exponential({exp_param}ms)
*.{talker}.app[{talker_app}].sendInterval = 9999s # burst consists of a single packet
*.{talker}.app[{talker_app}].messageLength = {frame_size}B - 58B # 58B = 8B (UDP) + 20B (IP) + 14B (ETH MAC) + 4B (Dot1Q) + 4B (ETH FCS) + 8B (ETH PHY)
*.{listener}.app[{listener_app}].typename = "UdpSinkApp"
*.{listener}.app[{listener_app}].io.localPort = {port}
"""

shaping_ini = """
*.{device}.hasEgressTrafficShaping = true
*.{device}.numPcapRecorders = 1
*.{device}.pcapRecorder[0].moduleNamePattersn = "eth[*]"
*.{device}.pcapRecorder[0].pcapFile = "results/{device}.pcap"
*.{device}.eth[*].macLayer.queue.numTrafficClasses = {queues}
"""

gcl_ini = """
# {exact_transmissions}
*.{device}.eth[{iface}].macLayer.queue.transmissionGate[{queue}].offset = {offset}ms
*.{device}.eth[{iface}].macLayer.queue.transmissionGate[{queue}].initiallyOpen = {initial}
*.{device}.eth[{iface}].macLayer.queue.transmissionGate[{queue}].durations = [{gcl}]
"""

psfp_ini_header = """
**.bridging.streamFilter.ingress.meter[*].typename = "SingleRateTwoColorMeter"
**.bridging.streamFilter.ingress.meter[*].committedInformationRate = 40Mbps
**.bridging.streamFilter.ingress.meter[*].committedBurstSize = 10kB
**.bridging.streamFilter.ingress.gate[*].initiallyOpen = false
**.bridging.streamFilter.ingress.gate[*].typename = "PeriodicGate"
**.bridging.streamFilter.ingress.typename = "{filter_type}"
"""

nts_map = "{{packetFilter: expr(udp.destPort == {dest_port}), stream: '{frame}'}}"
stg_map = "'{frame}': {id}"
fl_map = "'{id}': {value}"
gate_t = """*.{device}.bridging.streamFilter.ingress.gate[{id}].durations = [{durations}]
*.{device}.bridging.streamFilter.ingress.gate[{id}].offset = {offset}ms
"""

psfp_ini = """*.{device}.hasIngressTrafficFiltering = true
*.{device}.bridging.streamFilter.ingress.numStreams = {num_streams}
*.{device}.bridging.streamCoder.decoder.mapping = [{name_to_stream_mapping}]
*.{device}.bridging.streamFilter.ingress.classifier.mapping = {{{stream_to_gate_mapping}}}
{gates}"""

mkfirm_psfp_ini = """*.{device}.bridging.streamFilter.ingress.numMKFirmStreams = {num_streams}
*.{device}.bridging.streamFilter.ingress.classifier.mkFirmMapping = {{{stream_to_gate_mapping}}}
{gates}"""

STREAM_TO_MODULE_MAP = {}


def build_ini_file(
    tsn_config,
    streams,
    device_map,
    link_map,
    topology,
    ini_output,
    scenario,
    package,
    network_name,
    sim_time,
    repetitions,
    histogram_directory,
    delay_outliers,
):
    global PORT

    hyper_period = math.lcm(*[stream["period"] for stream in streams]) / 1e6
    ini = omnetpp_ini_header.format(
        scenario=scenario,
        package=package,
        network=network_name,
        sim_time=sim_time,
        repetitions=repetitions,
    )
    if delay_outliers:
        ini += random_delay_ini_header
    else:
        ini += histograms_ini_header.format(prefix=histogram_directory)

    ini_links = ""
    for link in topology["links"]:
        source = device_map[link["source"]]
        iface = source["ifaces"].index(device_map[link["target"]]["name"])
        ini_links += channel_ini.format(
            source=source["name"], iface=iface, datarate=link["data_rate"] / 1e6
        )
    for i in range(DETCOM):
        ini_links += detcom_channel_ini.format(id=i, datarate=link["data_rate"] / 1e6)

    ini_tt_streams = ""
    for stream in streams:
        stream["ports"] = []
        STREAM_TO_MODULE_MAP[stream["name"]] = []
        for frame in range(int(1e6 * hyper_period / stream["period"])):
            if f"{stream['name']}#{frame}" not in tsn_config["TALKERS"]:
                break

            stream["source"] = stream["route"][0][0]
            stream["target"] = stream["route"][-1][1]
            STREAM_TO_MODULE_MAP[stream["name"]].append(
                f"{network_name}.{device_map[stream['target']]['name']}.app[{device_map[stream['target']]['app']}].sink"
            )

            ini_tt_streams += tt_stream_ini.format(
                talker=device_map[stream["source"]]["name"],
                listener=device_map[stream["target"]]["name"],
                port=PORT,
                talker_app=device_map[stream["source"]]["app"],
                listener_app=device_map[stream["target"]]["app"],
                frame_size=stream["frame_size"],
                period=hyper_period,
                offset=tsn_config["TALKERS"][f"{stream['name']}#{frame}"] / 1e6,
                stream=stream["name"],
                stream_period=stream["period"] / 1e6,
            )

            device_map[stream["source"]]["identifier_entries"].append(
                pcp_ini_identifier_entry.format(stream=stream["name"], dest_port=PORT)
            )
            device_map[stream["source"]]["encoder_entries"].append(
                pcp_ini_encoder_entry.format(stream=stream["name"], pcp=stream["pcp"])
            )

            device_map[stream["source"]]["has_outgoing_streams"] = "true"
            device_map[stream["target"]]["app"] += 1
            device_map[stream["source"]]["app"] += 1
            stream["ports"].append(PORT)
            PORT += 1

    ini_talkers = ""
    for node_id in device_map:
        if device_map[node_id]["type"] != 0:
            continue
        node = device_map[node_id]
        ini_talkers += talker_ini.format(
            device=node["name"],
            has_outgoing_streams=node["has_outgoing_streams"],
            apps=node["app"],
        )
        if not node["has_outgoing_streams"]:
            continue
        ini_talkers += pcp_ini.format(
            talker=node["name"],
            identifier_entries=", ".join(node["identifier_entries"]),
            encoder_entries=", ".join(node["encoder_entries"]),
        )

    ini_bridges = ""
    for node_id in device_map:
        node = device_map[node_id]
        ini_bridges += shaping_ini.format(device=node["name"], queues=8)

    for port in tsn_config["GCL"]:
        for queue in tsn_config["GCL"][port]:
            assert tsn_config["GCL"][port][queue]["initial"] == 0

            bridge = port.split("[")[1].split(",")[0]
            for bridge_id in device_map:
                if device_map[bridge_id]["name"] == bridge:
                    break

            iface = device_map[bridge_id]["ifaces"].index(
                port.split(",")[1].split("]")[0]
            )
            durations = tsn_config["GCL"][port][queue]["durations"]
            offset = tsn_config["GCL"][port][queue]["offset"] / 1e6
            initial = tsn_config["GCL"][port][queue]["initial"] == 1

            exact_transmissions = {}
            for stream in tsn_config["EXACT"]:
                if port in tsn_config["EXACT"][stream]:
                    if tsn_config["EXACT"][stream][port][0] not in exact_transmissions:
                        exact_transmissions[tsn_config["EXACT"][stream][port][0]] = []
                    exact_transmissions[tsn_config["EXACT"][stream][port][0]].append(
                        stream
                    )

            ini_bridges += gcl_ini.format(
                device=bridge,
                iface=iface,
                queue=queue.split("Q")[1],
                offset=offset,
                initial="true" if initial else "false",
                gcl=", ".join([f"{round(d / 1e6, 6)}ms" for d in durations]),
                exact_transmissions=", ".join(
                    [
                        f"{{{', '.join(streams)}}}: {start/1e6}ms"
                        for start, streams in exact_transmissions.items()
                    ]
                ),
            )

    has_mkfirm_streams = "MK_FIRM_PSFP" in tsn_config
    ini_bridges += psfp_ini_header.format(
        filter_type=(
            "MKFirmIeee8021qFilter" if has_mkfirm_streams else "Ieee8021qFilter"
        )
    )

    for device in tsn_config["PSFP"]:

        def gates_fmt(id, open, close, period):
            offset = period - close
            durations = ", ".join(
                [
                    f"{(offset+open)/1e6}ms",
                    f"{(close-open)/1e6}ms",
                ]
            )
            return gate_t.format(
                device=device, id=id, durations=durations, offset=offset / 1e6
            )

        def add_psfp_entry(entry, entry_type, frame):
            stream_name = frame.split("#")[0]
            frame_id = int(frame.split("#")[1])
            stream = [s for s in streams if s["name"] == stream_name][0]
            frame_id = frame_id % len(stream["ports"])
            frame = f"{stream_name}-{frame_id}"
            decoder = nts_map.format(
                dest_port=stream["ports"][frame_id],
                frame=frame,
            )
            if decoder not in nts:
                nts.append(decoder)
            stg[entry_type].append(stg_map.format(frame=frame, id=n))
            if entry_type == "default":
                gates[entry_type] += gates_fmt(
                    n, entry["open"], entry["close"], 1e6 * hyper_period
                )
            else:
                gates[entry_type] += gates_fmt(
                    n,
                    entry["open"],
                    entry["close"],
                    stream["period"] * len(stream["mk_firm"]["mask"]),
                )

        n = 0
        nts = []
        stg = {"default": [], "mk_firm": []}
        gates = {"default": "", "mk_firm": ""}

        # start with default streams
        for psfp_entry in tsn_config["PSFP"][device]:
            for frame in psfp_entry["frames"]:
                add_psfp_entry(psfp_entry, "default", frame)
                n += 1

        # add entries for (m,k)-firm streams
        default_streams = n
        if has_mkfirm_streams and device in tsn_config["MK_FIRM_PSFP"]:
            for mkfirm_psfp_entry in tsn_config["MK_FIRM_PSFP"][device]:
                for frame in mkfirm_psfp_entry["frames"]:
                    add_psfp_entry(mkfirm_psfp_entry, "mk_firm", frame)
                    n += 1

        ini_bridges += psfp_ini.format(
            device=device,
            num_streams=default_streams,
            name_to_stream_mapping=", ".join(nts),
            stream_to_gate_mapping=", ".join(stg["default"]),
            gates=gates["default"],
        )
        ini_bridges += mkfirm_psfp_ini.format(
            device=device,
            num_streams=n - default_streams,
            stream_to_gate_mapping=", ".join(stg["mk_firm"]),
            gates=gates["mk_firm"],
        )

    ini = omnetpp_ini.format(
        header=ini,
        links=ini_links,
        talkers=ini_talkers,
        bridges=ini_bridges,
        streams=ini_tt_streams,
    )

    with open(ini_output, "w") as f:
        f.write(ini)


def main(raw_args=None):
    global OMNETPP_NED_MIN_POS, OMNETPP_NED_MAX_POS, PORT, DETCOM

    STREAM_TO_MODULE_MAP = {}

    OMNETPP_NED_MIN_POS = 200
    OMNETPP_NED_MAX_POS = 1000

    PORT = 2000
    DETCOM = 0

    parser = argparse.ArgumentParser(
        prog="Omnetpp Builder",
        description="Converts the Scheduler's Output in Omnetpp NED and INI files",
    )
    parser.add_argument("-t", "--topology_input", default="data/network.json")
    parser.add_argument("-s", "--streams_input", default="data/streams.json")
    parser.add_argument("-g", "--gcl_input", default="data/tsn_configuration.json")
    parser.add_argument("-ned", "--ned_output", default="data/network.ned")
    parser.add_argument("-ini", "--ini_output", default="data/omnetpp.ini")
    parser.add_argument("--package_name", default="test")
    parser.add_argument("--network_name", default="TestNetwork")
    parser.add_argument("--scenario", default="General")
    parser.add_argument("--simulation_time", type=int, default=10, help="in seconds")
    parser.add_argument("--repetitions", type=int, default=1, help="repetitions")
    parser.add_argument(
        "--histogram_directory",
        default=".",
        help="location (relative to ini file) of histograms",
    )
    parser.add_argument(
        "--delay_outliers",
        action="store_true",
        help="randomly sample delays at talker and 5G links differently from known histograms",
    )

    args = parser.parse_args(raw_args)

    topology = parse_json_file(args.topology_input)
    device_map, link_map = build_network_description_file(
        topology, args.ned_output, args.package_name, args.network_name
    )
    gcl = parse_json_file(args.gcl_input)
    streams = parse_json_file(args.streams_input)
    build_ini_file(
        gcl,
        streams,
        device_map,
        link_map,
        topology,
        args.ini_output,
        args.scenario,
        args.package_name,
        args.network_name,
        args.simulation_time,
        args.repetitions,
        args.histogram_directory,
        args.delay_outliers,
    )


if __name__ == "__main__":
    main()
