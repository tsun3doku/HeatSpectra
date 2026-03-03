#pragma once

class RuntimeCommands {
public:
    virtual ~RuntimeCommands() = default;

    virtual void toggleSimulation() = 0;
    virtual void pauseSimulation() = 0;
    virtual void resetSimulation() = 0;
};

class RuntimeQuery {
public:
    virtual ~RuntimeQuery() = default;

    virtual bool isSimulationActive() const = 0;
    virtual bool isSimulationPaused() const = 0;
};

