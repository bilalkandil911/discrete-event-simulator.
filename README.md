# Discrete-Event Queueing Simulator

A discrete-event simulation engine modeling a customer-service queueing system (e.g. bank tellers) — built in C++ for performance, with a Python bridge for running and visualizing simulations.

## Overview

The simulator models customers arriving at a service system, waiting in queue, and being served by one or more servers. It advances time event-by-event (arrivals, service starts, service completions) rather than in fixed time steps, producing accurate wait-time and utilization statistics without wasting cycles simulating idle time.

## Architecture

The project is split into two layers:

- **C++ simulation engine** — implements the core data structures and event-driven simulation loop. This is where the actual model logic and performance-critical work happens.
- **Python bridge (`app.py`)** — drives the C++ engine and handles running simulations and presenting results, without reimplementing the simulation logic itself.

```
main.cpp / simulator_bridge.cpp   →  simulation engine + bridge entry points
Models.h                          →  core domain types (Customer, Server, Event, etc.)
Queue.h                           →  custom queue data structure used to hold waiting customers
Simulator.h                       →  the discrete-event simulation loop and scheduling logic
app.py                            →  Python-side driver/interface for running simulations
Makefile                          →  builds the C++ engine
```

## Key Components

| File | Responsibility |
|---|---|
| `Models.h` | Defines the core entities in the simulation — customers, servers, and events |
| `Queue.h` | Custom queue implementation used to manage customers waiting for service |
| `Simulator.h` | The discrete-event engine: processes events in time order and updates system state |
| `main.cpp` | Entry point for running the C++ simulation directly |
| `simulator_bridge.cpp` | Exposes the simulation engine so it can be driven from Python |
| `app.py` | Python-side interface for configuring and running simulations |

## How It Works

1. Customers "arrive" according to the simulation's arrival logic, each generating an arrival event.
2. Arriving customers either start service immediately (if a server is free) or are placed in the queue.
3. When a server finishes with a customer, it pulls the next customer from the queue (if any) and starts their service.
4. The simulator processes these events in chronological order until the simulation ends, tracking metrics like average wait time, queue length, and server utilization.

## Getting Started

Build the C++ engine:
```bash
make
```

Run the simulation directly:
```bash
./sim
```

Or run it through the Python interface:
```bash
python app.py
```

## Data Structures Concepts Demonstrated

- Custom queue implementation (FIFO structure for waiting customers)
- Event-driven simulation using a time-ordered event structure
- Separation of simulation engine (C++) from presentation/control layer (Python)

## Author

Belal Kandil
