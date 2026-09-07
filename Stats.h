#ifndef STATS_H
#define STATS_H

#include <vector>
#include <numeric>
#include <algorithm>

struct SimStats {
    int    totalCustomers      = 0;
    int    customersServed     = 0;
    double totalWaitingTime    = 0.0;
    double totalServiceTime    = 0.0;
    double totalTimeInSystem   = 0.0;
    int    maxQueueLength      = 0;
    double sumQueueLength      = 0.0;  // accumulated each tick
    int    simulationDuration  = 0;

    // Per-tick queue lengths for the UI chart
    std::vector<int> queueLengthHistory;

    void recordQueueLength(int len, int tick) {
        sumQueueLength += len;
        maxQueueLength  = std::max(maxQueueLength, len);
        (void)tick;
        queueLengthHistory.push_back(len);
    }

    double avgWaitingTime()   const {
        return customersServed ? totalWaitingTime / customersServed : 0.0;
    }
    double avgServiceTime()   const {
        return customersServed ? totalServiceTime / customersServed : 0.0;
    }
    double avgTimeInSystem()  const {
        return customersServed ? totalTimeInSystem / customersServed : 0.0;
    }
    double avgQueueLength()   const {
        return simulationDuration ? sumQueueLength / simulationDuration : 0.0;
    }
    double throughput()       const {
        return simulationDuration ? (double)customersServed / simulationDuration : 0.0;
    }
};

#endif // STATS_H
