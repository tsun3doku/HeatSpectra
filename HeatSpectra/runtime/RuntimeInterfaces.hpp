#pragma once

class RuntimeQuery {
public:
    virtual ~RuntimeQuery() = default;

    virtual bool isSimulationActive() const = 0;
    virtual bool isSimulationPaused() const = 0;
};
