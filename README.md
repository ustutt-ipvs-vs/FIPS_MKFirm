# (m,k)-firm Evaluation Policy for FIPS
You can find the original FIPS repository [here](https://github.com/DETERMINISTIC6G/fips).

# Building the Project 

## Docker/Podman Setup
This is the recommended way of deployment if you simply want to reproduce the evaluation results and your compiler does not support C++23.
Start by pulling or building the image via:
```bash
# Pull Image
$ podman pull ghcr.io/ustutt-ipvs-vs/fips_mkfirm:v1.0
$ podman tag ghcr.io/ustutt-ipvs-vs/fips_mkfirm:v1.0 mkfirm_fips

# Build Image Manually (Alternative)
$ podman built -t mkfirm_fips .
```
this will download the latest gcc docker image, install the required packages, and setup a python virtual environment.
You can then open a shell in the container with
```bash
$ podman run -v ./data:/usr/src/fips/data -it mkfirm_fips
```

## Local Build
Make sure your compiler supports C++23, as described here [detailed building guide](documentation/build.md).
Build the project (directly or after the above docker setup) with its test cases and benchmarks via:
```bash
$ mkdir release && cd release
$ cmake ..
$ make -j
```

# Quick Start to Run FIPS with the (m,k)-firm Elevation Policy
After the above building steps, you should be able to see the binary `benchmarks/mk_firm`.
Check out its help menu:
```bash
# located in ./release
$ ./benchmarks/mk_firm -h 
Usage: Heuristics for Wireless IEEE 802.1Qbv Scheduling [--help] [--version] --network VAR --streams VAR --output_normal VAR --output_mkfirm VAR

Optional arguments:
  -h, --help       shows help message and exits 
  -v, --version    prints version information and exits 
  -n, --network    specify the network topology [nargs=0..1] [default: "../data/network.json"]
  -s, --streams    specify the time-triggered streams [nargs=0..1] [default: "../data/streams.json"]
  --output_normal  output file of the normal TSN configuration [nargs=0..1] [default: "../data/tsn_configuration.json"]
  --output_mkfirm  output file of the (m,k)-firm TSN configuration [nargs=0..1] [default: "../data/mkfirm_configuration.json"]
```
Naturally, we need to provide the scheduler with a network topology and the stream specification.
We generate these input files in the following:

## Building a Network and Creating Streams

First, make sure you have all requirements installed, e.g., via
```bash
$ python -m venv ./venv
$ source venv/bin/activate
$ pip install -Ur python_requirements.txt
```

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

## Running FIPS with the (m,k)-firm Elevation Policy
Run the following to compute a feasible TSN schedule that tries to incorporate as many of the streams as possible, and to store the resulting TSN configuration (GCL & PSFP configuration) in `./data/tsn_configuration.json`:
```bash
$ cd release
$ ./benchmarks/mk_firm -n ../data/network.json -s ../data/streams.json --output_normal ../data/tsn_configuration.json --output_mkfirm ../data/mkfirm_configuration.json
Result: CORE_CT00 true
...
Result: AGV0_CORE_09 true
Scheduled 30 streams: total 19ms, augmentation 211us
```

## Examining the TSN Configuration
We provide a relatively verbose output, including:
 - The GCL configuration at each egress port and each queue
 - The PSFP configuration (including our (m,k)-firm Extension) at each bridge
 - The initial transmission offset at each talker 
 - The expected arrival interval at the listeners
 - The exact transmission start for each frame at each hop (for validation)
An in-depth explanation of each component is given [here](documentation/tsn_configuration.md).

# Reproducing the Evaluation Results
In case you want to modify the benchmarking scripts (recommended to decrease their runtime), start by cloning the repository:
```
$ git clone https://github.com/ustutt-ipvs-vs/FIPS_MKFirm.git
```
The following provides a guide to reproduce the individual benchmarks with as little manual intervention as possible.
We use podman for this purpose; however, you can follow the same commands with docker or apptainer.

## Simulation results (Section VI-C: 5G Delay Outliers)
You may want to change the configuration variables of `scripts/simulation_5G.py` to reduce the simulation time.
For instance, set to have a very fast simulation of 100 hypercycles
```python
REPETITIONS = 1
SIM_TIME = 2  # 2s = 100 hypercycles
SIM_BATCHES = 1  # run X repetitions of each simulation in parallel
```
Afterwards, you have to mount both `./data` and `./scripts` and can run the simulation as follows
```bash
$ podman run -v ./data:/usr/src/fips/data -v ./scripts:/usr/src/fips/scripts -it mkfirm_fips
/usr/src/fips# python scripts/simulation_5G.py
```
The results of the simulation are now available under `./data/mkfirm_simulations/csv`.
They can also be further compressed to the same representation as in the paper, using
```bash
/usr/src/fips# python scripts/omnetpp_pcap_analysis.py analyze
```
The script prints the first observed (m,k)-firm violations, i.e., the 5G delay outliers that were observed for which the required E2E latency can no longer be upheld.

## Scalability Results under different mu-Patterns (Section VI-D1)
```bash
$ podman run -v ./data:/usr/src/fips/data -it mkfirm_fips
/usr/src/fips# python scripts/scalability.py
```
The script continuously prints the updated average of rejected streams per configuration.

## Simulation results (Section VI-D2: Talker Jitter + 5G Delay Outliers)
You may want to change the configuration variables of `scripts/simulation_release_and_5G.py` to reduce the simulation time (same as above).
Afterwards, you have to mount both `./data` and `./scripts` and can run the simulation as follows
```bash
$ podman run -v ./data:/usr/src/fips/data -v ./scripts:/usr/src/fips/scripts -it mkfirm_fips
/usr/src/fips# python scripts/simulation_release_and_5G.py
```
The results of the simulation are now available under `./data/skipfactor_simulations/csv`.
They can also be further compressed to the same representation as in the paper, using
```bash
/usr/src/fips# python scripts/omnetpp_pcap_analysis.py reliability
```

# Library Usage Tutorial
We provide a brief overview of how the library can be used.
You can also directly look at the files contained in *./benchmarks*.

## Initializing the Network and Streams
Loading the network and streams in *libtsndgm* is as simple as calling:
```cpp
  auto network = NetworkTopology(network_file);
  auto stream_storage = StreamStorage(stream_file, network);
```
If, however, you are interested in building custom networks and streams, please review the source files in *./tests*. 

## FIPS: Running the Heuristics and Deriving the TSN Configuration
FIPS currently supports incremental heuristics that add one stream to an existing TSN schedule at a time. 
```cpp
  IncrementalHeuristic heuristic(&stream_storage, &network);
  for (StreamId id = 0; id < stream_storage.streams.size(); id++) {
    auto accepted = heuristic.add_stream(id);
    std::println("Result: {} {}", stream_storage.streams[id].name, accepted);
  }
  auto g = std::move(heuristic.g);
```
Alternative initial heuristics are defined in *src/heuristic/initial/initial.h*.

## Deriving the TSN Configuration

This includes the GCLs, the PSFP configuration, and additional metadata.
If you'd like to return the plain FIPS configuration, use
```cpp
  auto tsn_configuration = g.derive_tsn_configuration<CriticalPathConfiguration>();
  // optional: tsn_configuration.add_meta_data("field", "value");
  tsn_configuration.dump_to_file(tsn_config_file);
```

To derive the additional configuration for the (m,k)-firm Elevation Policy, use
```cpp
  auto mkfirm_configuration = g.derive_tsn_configuration<MKFirmConfiguration>();
  // optional: tsn_configuration.add_meta_data("field", "value");
  mkfirm_configuration.dump_to_file(mkfirm_config_file);
```

# Cite this Project
Coming soon
