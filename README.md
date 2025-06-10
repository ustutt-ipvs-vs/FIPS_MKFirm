# Full Interleaving Packet Scheduling (FIPS)
FIPS is a wireless-friendly TSN scheduler that provides formal end-to-end QoS guarantees at scale.
This repository contains a proof-of-concept implementation and provides Docker images to facilitate reproducibility of our evaluation results.
Feel free to contact the [author](mailto:simon.egger@ipvs.uni-stuttgart.de) or open an issue if you have any questions.

# Building the Project 
In case you have trouble building C++23 projects in general, please refer to the [detailed building guide](documentation/build.md).

## Local Build
Build the project (directly or after the above docker setup) with its test cases and benchmarks via:
```bash
$ mkdir release && cd release
$ cmake ..
$ make -j
```

## Docker/Podman Setup
This is the recommended way of deployment if you simply want to reproduce the evaluation results and your compiler does not support C++23.
Start by building the image via:
```bash
$ podman built -t fips .
```
this will download the latest gcc docker image, install the required packages, and setup a python virtual environment.
You can then open a shell in the container with
```bash
$ podman run -v .:/usr/src/fips -it fips
```
Then, commence with the same commands as with the [Local Build](#local-build).

# Quick Start to Run FIPS
After the above building steps, you should be able to see the binary `benchmarks/heuristic`.
Check out its help menu:
```bash
# located in ./release
$ ./benchmarks/heuristic -h 
Usage: Heuristics for Wireless IEEE 802.1Qbv Scheduling [--help] [--version] --network VAR --streams VAR --output VAR [--strict_tempor
al_isolation]

Optional arguments:
  -h, --help                         shows help message and exits 
  -v, --version                      prints version information and exits 
  -n, --network                      specify the network topology [nargs=0..1] [default: "../data/network.json"]
  -s, --streams                      specify the time-triggered streams [nargs=0..1] [default: "../data/streams.json"]
  -o, --output                       output file of the final TSN configuration [nargs=0..1] [default: "../data/tsn_configuration.json
"]
  -sti, --strict_temporal_isolation  use strict temporal isolation constraints (i.e., no batching) 
```
Naturally, we need to provide the scheduler with a network topology and the stream specification.
We generate these input files in the following:

## Building a Network and Creating Streams
We provide a helper script that builds and simple network of an automated guided vehicle (AGV) use case.
```bash
# Show help menu 
$ python scripts/agv_network_builder.py -h
...

# Example with 10 uplink/downlink streams and 5+5 internal streams
$ python scripts/agv_network_builder.py -agv_wt_out 10 -agv_wt_in 10 -agv_ct 5 -core_ct 5
generated network with 30 streams
```
This will produce the following network:

![data/network.png](./data/network.png)

Be sure to run the above commands in the project root directory.
By default, the resulting `network.json` and `streams.json` file will be stored in `./data`.

## Running FIPS 
Run the following to compute a feasible TSN schedule that tries to incorporate as many of the streams as possible, and to store the resulting TSN configuration (GCL & PSFP configuration) in `./data/tsn_configuration.json`:
```bash
$ cd release
$ ./benchmarks/heuristic -n ../data/network.json -s ../data/streams.json -o ../data/tsn_configuration.json
Result: CORE_CT00 true
...
Result: AGV0_CORE_09 true
Scheduled 30 streams
```

## Examining the TSN Configuration
We provide a relatively verbose output, including:
 - The GCL configuration at each egress port and each queue
 - The PSFP configuration at each bridge
 - The initial transmission offset at each talker 
 - The expected arrival interval at the listeners
 - The exact transmission start for each frame at each hop (for validation)
An in-depth explanation of each component is given [here](documentation/tsn_configuration.md).

# Reproduce the Evaluation Results
## Scalability results
```bash
$ podman run -v .:/usr/src/fips -it fips
/usr/src/fips# python scripts/scalability.py
```

## Simulation results
```bash
$ podman run -v .:/usr/src/fips -it fips
/usr/src# source omnetpp/omnetpp/setenv
/usr/src/fips# python scripts/simulation.py
```

# Library Usage Tutorial
## Initializing the Network and Streams
Loading the network and streams in *libtsndgm* is as simple as calling:
```cpp
  auto network = NetworkTopology(network_file);
  auto stream_storage = StreamStorage(stream_file, network);
```
If, however, you are interested in building custom networks and streams, please review the source files in *./tests*. 

## Running the Heuristics and Deriving the TSN Configuration
We currently support incremental heuristics that add one stream to an existing TSN schedule at a time. 
```cpp
  IncrementalHeuristic heuristic(&stream_storage, &network);
  for (StreamId id = 0; id < stream_storage.streams.size(); id++) {
    auto accepted = heuristic.add_stream(id);
    std::println("Result: {} {}", stream_storage.streams[id].name, accepted);
  }
  auto g = std::move(heuristic.g);
```
Alternative initial heuristics are defined in *src/heuristic/initial/initial.h*.

The TSN configuration (GCLs, PSFP configuration, and additional metadata) can be computed as follows:
```cpp
  auto tsn_configuration = g.derive_tsn_configuration();
  tsn_configuration.add_meta_data("field", "value");
  tsn_configuration.dump_to_file(tsn_config_file);
```

# Cite this Project
There is currently a preprint available (this will later be updated to proceedings of IWQoS 2025):
```
@misc{egger2025endtoendreliabilitywirelessieee,
      title={End-to-End Reliability in Wireless IEEE 802.1Qbv Time-Sensitive Networks}, 
      author={S. Egger and J. Gross and J. Sachs and G. P. Sharma and C. Becker and F. Dürr},
      year={2025},
      eprint={2502.11595},
      archivePrefix={arXiv},
      primaryClass={cs.NI},
      url={https://arxiv.org/abs/2502.11595}, 
}
```
