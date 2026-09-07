#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "Queue.h"
#include "Models.h"
#include "Stats.h"

#include <vector>
#include <random>
#include <stdexcept>

// ─── Configuration 
struct SimConfig {
    int numServers       = 2;    // number of service counters
    int arrivalRate      = 3;    // avg ticks between arrivals (higher = fewer customers)
    int minServiceTime   = 2;    // min ticks to serve one customer
    int maxServiceTime   = 6;    // max ticks to serve one customer
    int simulationTime   = 100;  // total clock ticks to run
    int randomSeed       = 42;   // for reproducibility (0 = truly random)
};

// ─── Simulator 
class Simulator {
public:
    explicit Simulator(const SimConfig& cfg) : cfg(cfg) {
        if (cfg.numServers < 1)
            throw std::invalid_argument("Must have at least 1 server");
        if (cfg.arrivalRate < 1)
            throw std::invalid_argument("Arrival rate must be >= 1");
        if (cfg.minServiceTime < 1 || cfg.maxServiceTime < cfg.minServiceTime)
            throw std::invalid_argument("Invalid service time range");
        if (cfg.simulationTime < 1)
            throw std::invalid_argument("Simulation time must be >= 1");

        // Seed RNG for reproducibility
        unsigned seed = (cfg.randomSeed == 0)
            ? static_cast<unsigned>(std::random_device{}())
            : static_cast<unsigned>(cfg.randomSeed);
        rng.seed(seed);
    }

    SimStats run() {
        SimStats stats;
        stats.totalCustomers     = 0;
        stats.simulationDuration = cfg.simulationTime;

        Queue<Customer>      waitingQueue;
        std::vector<Server>  servers;
        servers.reserve(cfg.numServers);
        for (int i = 0; i < cfg.numServers; i++)
            servers.emplace_back(i + 1);

        // Distribution objects
        std::exponential_distribution<double> arrivalDist(1.0 / cfg.arrivalRate);
        std::uniform_int_distribution<int>    serviceDist(cfg.minServiceTime, cfg.maxServiceTime);

        int nextArrivalTick = static_cast<int>(arrivalDist(rng));
        int nextCustomerId  = 1;

        // Track which customer is on which server (for logging)
        std::vector<Customer> activeCustomers(cfg.numServers);

        for (int clock = 0; clock < cfg.simulationTime; clock++) {

            //  1 Generate arrivals
            while (clock >= nextArrivalTick) {
                int duration = serviceDist(rng);
                Customer c(nextCustomerId++, nextArrivalTick, duration);
                waitingQueue.enqueue(c);
                stats.totalCustomers++;
                // Schedule next arrival
                int gap = std::max(1, static_cast<int>(arrivalDist(rng)));
                nextArrivalTick += gap;
            }

            // 2 Free servers whose job is done
            for (int s = 0; s < cfg.numServers; s++) {
                if (servers[s].isBusy && clock >= servers[s].availableAt) {
                    // Finalise customer stats
                    Customer& done = activeCustomers[s];
                    done.departureTime = clock;
                    stats.customersServed++;
                    stats.totalWaitingTime  += done.waitingTime();
                    stats.totalServiceTime  += done.serviceDuration;
                    stats.totalTimeInSystem += done.totalTimeInSystem();
                    servers[s].isBusy = false;
                    servers[s].customersServed++;
                    servers[s].totalBusyTicks += done.serviceDuration;
                }
            }

            // 3 Assign waiting customers to free servers
            for (int s = 0; s < cfg.numServers; s++) {
                if (!servers[s].isBusy && !waitingQueue.isEmpty()) {
                    Customer c = waitingQueue.dequeue();
                    c.serviceStartTime   = clock;
                    servers[s].isBusy    = true;
                    servers[s].availableAt = clock + c.serviceDuration;
                    activeCustomers[s]   = c;
                }
            }

            // 4.Record queue snapshot
            stats.recordQueueLength(waitingQueue.size(), clock);
        }

        // 5 Drain any remaining in queue (count as not served)
        // They are already counted in totalCustomers

        return stats;
    }

    const SimConfig& config() const { return cfg; }

private:
    SimConfig          cfg;
    std::mt19937       rng;
};

#endif // SIMULATOR_H
