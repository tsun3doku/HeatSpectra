#pragma once

class RuntimeModelComputeTransport;
class RuntimePointComputeTransport;
class RuntimeRemeshComputeTransport;
class RuntimeVoronoiComputeTransport;
class RuntimeHeatComputeTransport;
class RuntimeModelDisplayTransport;
class RuntimePointDisplayTransport;
class RuntimeRemeshDisplayTransport;
class RuntimeVoronoiDisplayTransport;
class RuntimeHeatDisplayTransport;

// Runtime transport wiring only. Authored graph state and payload registries are not
// runtime connections and must be supplied explicitly to their owning graph layer.
struct RuntimeConnections {
    RuntimeModelComputeTransport* modelComputeTransport = nullptr;
    RuntimePointComputeTransport* pointComputeTransport = nullptr;
    RuntimeRemeshComputeTransport* remeshComputeTransport = nullptr;
    RuntimeVoronoiComputeTransport* voronoiComputeTransport = nullptr;
    RuntimeHeatComputeTransport* heatComputeTransport = nullptr;

    RuntimeModelDisplayTransport* modelDisplayTransport = nullptr;
    RuntimePointDisplayTransport* pointDisplayTransport = nullptr;
    RuntimeRemeshDisplayTransport* remeshDisplayTransport = nullptr;
    RuntimeVoronoiDisplayTransport* voronoiDisplayTransport = nullptr;
    RuntimeHeatDisplayTransport* heatDisplayTransport = nullptr;
};
