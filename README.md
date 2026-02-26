# (m,k)-firm Evaluation Policy for FIPS

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
Start by pulling or building the image via:
```bash
# Pull Image
$ podman pull gitlab-vs.informatik.uni-stuttgart.de:5050/emergency_traffic/mkfirm_fips:latest
$ podman tag gitlab-vs.informatik.uni-stuttgart.de:5050/emergency_traffic/mkfirm_fips:latest mkfirm_fips

# Build Image Manually (Alternative)
$ podman built -t mkfirm_fips .
```
this will download the latest gcc docker image, install the required packages, and setup a python virtual environment.
You can then open a shell in the container with
```bash
$ podman run -v ./data:/usr/src/fips/data -it mkfirm_fips
```

# Reproducing the Evaluation Results

## Scalability Results under different mu-Patterns
```bash
$ podman run -v ./data:/usr/src/fips/data -it mkfirm_fips
/usr/src/fips# python scripts/scalability.py
```

## Simulation results (5G Delay Outliers)
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

## Simulation results (Talker Jitter + 5G Delay Outliers)
You may want to change the configuration variables of `scripts/simulation_release_and_5G.py` to reduce the simulation time (same as above).
Afterwards, you have to mount both `./data` and `./scripts` and can run the simulation as follows
```bash
$ podman run -v ./data:/usr/src/fips/data -v ./scripts:/usr/src/fips/scripts -it mkfirm_fips
/usr/src/fips# python scripts/simulation_release_and_5G.py
```
The results of the simulation are now available under `./data/skipfactor_simulations/csv`.
