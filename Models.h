#ifndef MODELS_H
#define MODELS_H

//  Customer Blueprint
struct Customer {
    int id;
    int arrivalTime;
    int serviceStartTime;
    int serviceDuration;
    int departureTime;

    Customer() : id(0), arrivalTime(0), serviceStartTime(0),
                 serviceDuration(0), departureTime(0) {}

    Customer(int id, int arrivalTime, int serviceDuration)
        : id(id), arrivalTime(arrivalTime), serviceStartTime(0),
          serviceDuration(serviceDuration), departureTime(0) {}

    int waitingTime()   const { return serviceStartTime - arrivalTime; }
    int totalTimeInSystem() const { return departureTime - arrivalTime; }
};

// ─── Server Blueprint
struct Server {
    int  id;
    bool isBusy;
    int  availableAt;      // clock tick when this server becomes free
    int  customersServed;
    int  totalBusyTicks;

    Server() : id(0), isBusy(false), availableAt(0),
               customersServed(0), totalBusyTicks(0) {}

    explicit Server(int id)
        : id(id), isBusy(false), availableAt(0),
          customersServed(0), totalBusyTicks(0) {}
};

#endif // MODELS_H
