# GCL Configuration 
The gate control list is configured for each egress queue, i.e.,
 - when the gate is open and frames are eligible for transmission, and
 - when the gate is closed and frames have to wait in the queue.
For simplicity, we assume there is only one link between two devices, allowing us to denote the egress port in the form `[AGV0_01,AGV0_04]`.
In turn, the queues are denoted by `Q0`-`Q7`.
The individual entries are formatted as in OMNeT++/INET to simplify conversion (see [here](https://doc.omnetpp.org/inet/api-current/neddoc/inet.queueing.gate.GateControlList.html)).
All entries are given in nanoseconds.
```json
{
    ...
    "GCL": {
        ...
        "[AGV0_01,AGV0_04]": {
            "Q6": {
                "durations": [
                    19982080,
                    17920
                ],
                "initial": 0,
                "offset": 4930790
            },
            "Q7": {
                "durations": [
                    4991040,
                    8960,
                    4991040,
                    8960,
                    4991040,
                    8960,
                    4991040,
                    8960
                ],
                "initial": 0,
                "offset": 4982030
            }
        },
        ...
    },
    ...
}
```
 
# PSFP Configuration
The PSFP configuration provides each bridge with the expected arrival interval of each frame. 
That is, FIPS requires the bridge `AGV0_00` to discard the frame `AGV0_CORE_00#0` (the suffix `#0` denotes the frame index of the stream `AGV0_CORE_00`) if and only if it arrives outside the expected interval `[34.980us, 53.860us]`.
This is mainly to avoid unintended queueing backlogs at `AGV0_00` (imagine what happens if `AGV0_CORE_00#0` misses its transmission slot) and the resulting QoS violations of other high-criticality traffic.
```json
{
    ...
    "PSFP": {
        "AGV0_00": [
            {
                "close": 53860,
                "frames": [
                    "AGV0_CORE_00#0",
                    "AGV0_CORE_08#0",
                    "AGV0_CORE_09#0"
                ],
                "open": 34980
            },
            {
                "close": 62820,
                "frames": [
                    "AGV0_CORE_01#0",
                    "AGV0_CORE_05#0",
                    "AGV0_CORE_06#0"
                ],
                "open": 43940
            },
            ...
        ],
        ...
    },
    ...
}
```

# Talker Configuration
Compared to the GCL configuration at the first hop, it is often helpful to provide additional information about when each frame should start its transmission (e.g., for coordination with the application). 
The underlying semantic is: At time `8.960us`, the application of `AGV0_CORE_00#0` enqueues the frame at the egress port while there are not other frames enqueued.
Realistically, this requires an additional daemon at the talker that ensures the above semantic (e.g., the correct frame ordering and their correct release time).
```json
{
    ...
    "TALKERS": {
        "AGV0_CORE_00#0": 8960,
        "AGV0_CORE_01#0": 8960,
        "AGV0_CORE_02#0": 54720,
        "AGV0_CORE_03#0": 45760,
        "AGV0_CORE_04#0": 45760,
        ...
    },
    ...
}
```

# Listener Configuration
Finally, the listener configuration specifies the expected arrival interval of each frame at the listener.
```json
{
    ...
    "LISTENERS": {
        "AGV0_CORE_00#0": [
            13399340,
            13409260
        ],
        "AGV0_CORE_01#0": [
            13399340,
            13418220
        ],
        ...
    },
    ...
}
```
