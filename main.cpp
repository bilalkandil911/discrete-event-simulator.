#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include <random>
#include <windows.h>

#include "Simulator.h"

// ── ANSI helpers 
#define ESC "\033["
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define AMBER       "\033[33m"
#define AMBERBOLD   "\033[1;33m"
#define GREEN       "\033[32m"
#define GREENBOLD   "\033[1;32m"
#define RED         "\033[1;31m"
#define CYAN        "\033[36m"
#define CYANBOLD    "\033[1;36m"
#define WHITEBOLD   "\033[1;37m"
#define GREY        "\033[90m"

static inline void clearScreen()        { std::cout << "\033[2J\033[H"; }
static inline void moveTo(int r, int c) { std::cout << "\033[" << r << ";" << c << "H"; }
static inline void hideCursor()         { std::cout << "\033[?25l"; }
static inline void showCursor()         { std::cout << "\033[?25h"; }


static void onExit(int) {
    showCursor(); clearScreen(); moveTo(1,1);
    std::cout << RESET CYANBOLD "  Goodbye!\n" RESET;
    std::exit(0);
}

// ── String utilities 
static std::string repeat(char c, int n) {
    if (n <= 0) return "";
    return std::string(n, c);
}
static std::string padR(std::string s, int w) {
    if ((int)s.size() >= w) return s.substr(0,w);
    return s + std::string(w-s.size(), ' ');
}
static std::string padL(std::string s, int w) {
    if ((int)s.size() >= w) return s;
    return std::string(w-s.size(), ' ') + s;
}
static std::string fmtF(double v, int dp=2) {
    std::ostringstream ss; ss << std::fixed << std::setprecision(dp) << v;
    return ss.str();
}
// Strip ANSI to get printable length
static int vis(const std::string& s) {
    int l=0; bool e=false;
    for (char c : s) { if(e){if(std::isalpha(c))e=false;}else if(c=='\033')e=true;else++l;}
    return l;
}
static std::string vpad(const std::string& s, int w) {
    int vl=vis(s); return vl>=w ? s : s+std::string(w-vl,' ');
}

// ── Config prompt 
struct Config {
    int numServers=2, arrivalRate=3, minService=2, maxService=6, simTime=100, speedMs=120;
};

static int readInt(const char* prompt, int def, int lo, int hi) {
    std::cout << AMBER "  " << prompt << GREY " [" << lo << "-" << hi << ", def=" << def << "]: " RESET;
    std::string ln; std::getline(std::cin, ln);
    if (ln.empty()) return def;
    try { int v=std::stoi(ln); return v<lo?lo:(v>hi?hi:v); } catch(...){ return def; }
}

static Config promptConfig() {
    clearScreen(); moveTo(1,1);
    std::cout << "\n"
        AMBERBOLD "  ╔══════════════════════════════════════════╗\n"
        "  ║    QUEUING SYSTEM SIMULATOR              ║\n"
        "  ║    Data Structures  ·  Spring 2026       ║\n"
        "  ╚══════════════════════════════════════════╝\n" RESET
        "\n" GREY "  Set simulation parameters:\n\n" RESET;

    Config c;
    c.numServers  = readInt("Servers (parallel counters)", 2, 1, 8);
    c.arrivalRate = readInt("Avg ticks between arrivals ", 3, 1,20);
    c.minService  = readInt("Min service time (ticks)   ", 2, 1,10);
    c.maxService  = readInt("Max service time (ticks)   ", 6, c.minService, 20);
    c.simTime     = readInt("Simulation length (ticks)  ",100,20,500);

    std::cout << "\n" GREY "  Speed: " RESET
              << "1=Slow  2=Normal  3=Fast  4=Instant\n";
    int sp = readInt("Choose speed", 2, 1, 4);
    switch(sp){ case 1:c.speedMs=400;break; case 2:c.speedMs=120;break;
                case 3:c.speedMs=30; break; case 4:c.speedMs=0;   break; }
    return c;
}

// ── Scenario batch
static void runScenarios() {
    struct Sc{ std::string name; SimConfig cfg; };
    std::vector<Sc> list = {
        {"1 Srv | Slow arrivals",  {1,5,2,4,200,42}},
        {"1 Srv | Fast arrivals",  {1,2,2,4,200,42}},
        {"2 Srv | Fast arrivals",  {2,2,2,4,200,42}},
        {"3 Srv | Fast arrivals",  {3,2,2,4,200,42}},
        {"2 Srv | Long service",   {2,3,5,10,200,42}},
        {"4 Srv | Long service",   {4,3,5,10,200,42}},
    };
    std::cout << "\n"
        AMBERBOLD "  ╔═══════════════════════════════════════════════════════════════╗\n"
        "  ║  SCENARIO COMPARISON                                          ║\n"
        "  ╚═══════════════════════════════════════════════════════════════╝\n" RESET "\n"
        << "  " GREY
        << padR("Scenario",22) << padL("Srv",4) << padL("Arrived",9)
        << padL("Served",9)    << padL("AvgWait",10) << padL("AvgQueue",10)
        << padL("Throughput",12) << RESET "\n"
        << "  " GREY << repeat('-',76) << RESET "\n";

    for (auto& s : list) {
        Simulator sim(s.cfg); SimStats st=sim.run();
        double aw=st.avgWaitingTime(), aq=st.avgQueueLength(), th=st.throughput();
        double pct=st.customersServed/(double)std::max(1,st.totalCustomers);
        const char* wc=(aw<3)?GREEN:(aw<10?AMBER:RED);
        const char* qc=(aq<2)?GREEN:(aq<8?AMBER:RED);
        const char* sc2=(pct>.9)?GREENBOLD:(pct>.7?AMBER:RED);
        std::cout
            << "  " WHITEBOLD << padR(s.name,22) << RESET
            << GREY << padL(std::to_string(s.cfg.numServers),4) << RESET
            << CYAN << padL(std::to_string(st.totalCustomers),9) << RESET
            << sc2  << padL(std::to_string(st.customersServed),9) << RESET
            << wc   << padL(fmtF(aw)+"t",10) << RESET
            << qc   << padL(fmtF(aq),10) << RESET
            << GREY << padL(fmtF(th,3),12) << RESET "\n";
    }
    std::cout << "  " GREY << repeat('-',76) << RESET "\n";
}

// ── Bar chart 
static void drawBarChart(const std::vector<int>& hist, int W, int H) {
    if (hist.empty()) {
        for(int r=0;r<H;r++) std::cout << "    " GREY << repeat('.',W) << RESET "\n";
        return;
    }
    int n = std::min((int)hist.size(), W);
    std::vector<int> sl(hist.end()-n, hist.end());
    int maxV = *std::max_element(sl.begin(),sl.end());
    if (maxV==0) maxV=1;

    for (int row=H-1; row>=0; row--) {
        // Y-axis label
        if (row==H-1) std::cout << "  " GREY << padL(std::to_string(maxV),3) << " " RESET;
        else if (row==0) std::cout << "  " GREY "  0 " RESET;
        else std::cout << "      ";

        for (int col=0; col<n; col++) {
            int barH = (int)std::round((double)sl[col]/maxV*H);
            int barRow = H-1-row;
            if (barRow < barH) {
                double r = (double)sl[col]/maxV;
                if      (r < 0.40) std::cout << GREENBOLD  << "#" << RESET;
                else if (r < 0.75) std::cout << AMBERBOLD  << "#" << RESET;
                else               std::cout << RED        << "#" << RESET;
            } else {
                std::cout << GREY "." RESET;
            }
        }
        for (int col=n;col<W;col++) std::cout << GREY "." RESET;
        std::cout << "\n";
    }
    std::cout << "    " GREY << repeat('-',W) << RESET "\n";
    std::string xl="t-"+std::to_string(n), xr="now";
    int gap=W-(int)xl.size()-(int)xr.size();
    std::cout << "    " GREY << xl << std::string(std::max(0,gap),' ') << xr << RESET "\n";
}

// ── Customer chip colours
static const char* CHIP_COL[] = {
    "\033[33m","\033[32m","\033[36m","\033[35m",
    "\033[31m","\033[34m","\033[1;33m","\033[1;36m"
};
static std::string chips(const std::vector<Customer>& arr, int maxV) {
    std::string out;
    int show=std::min((int)arr.size(),maxV);
    for(int i=0;i<show;i++){
        out += CHIP_COL[arr[i].id%8];
        out += "["+padL(std::to_string(arr[i].id),2)+"]";
        out += RESET;
        if(i<show-1) out += GREY "-" RESET;
    }
    if ((int)arr.size()>maxV)
        out += GREY " +" + std::to_string(arr.size()-maxV) + " more" RESET;
    if (arr.empty()) out += GREY "(empty)" RESET;
    return out;
}

// ── Live simulation pressing enter to start
static void runLiveSim(const Config& cfg) {
    // Build SimConfig to match existing Simulator.h interface
    SimConfig sc;
    sc.numServers     = cfg.numServers;
    sc.arrivalRate    = cfg.arrivalRate;
    sc.minServiceTime = cfg.minService;
    sc.maxServiceTime = cfg.maxService;
    sc.simulationTime = cfg.simTime;
    sc.randomSeed     = 0;

    // Tick-by-tick simulation
    std::mt19937 rng(std::random_device{}());
    std::exponential_distribution<double> arrDist(1.0/sc.arrivalRate);
    std::uniform_int_distribution<int>    svcDist(sc.minServiceTime, sc.maxServiceTime);

    Queue<Customer>     waitQ;
    std::vector<Server> servers;
    for(int i=0;i<sc.numServers;i++) servers.emplace_back(i+1);
    std::vector<Customer> active(sc.numServers);
    std::vector<bool>     slotBusy(sc.numServers, false);

    SimStats stats;
    stats.simulationDuration = sc.simulationTime;

    int nextId=1, nextArr=(int)std::max(1.0, arrDist(rng));

    const int HIST=60, CHART_H=8, LOG_N=6;
    std::vector<int> qHist;
    std::vector<std::string> elog;

    auto addLog = [&](const std::string& line){
        elog.push_back(line);
        if((int)elog.size()>LOG_N) elog.erase(elog.begin());
    };

    hideCursor(); clearScreen();

    for (int clock=0; clock<sc.simulationTime; clock++) {

        // 1 Arrivals
        while (clock >= nextArr) {
            int dur = svcDist(rng);
            Customer c(nextId++, nextArr, dur);
            waitQ.enqueue(c); stats.totalCustomers++;
            nextArr += std::max(1,(int)arrDist(rng));
            addLog(std::string(GREY)+"[T"+padL(std::to_string(clock),3)+"] "
                  +RESET+CYAN+"ARRIVE "+RESET
                  +"Customer #"+std::to_string(c.id)+GREY+" queued"+RESET);
        }

        // 2 Release done servers
        for (int s=0;s<sc.numServers;s++){
            if(slotBusy[s] && clock>=servers[s].availableAt){
                Customer& d=active[s]; d.departureTime=clock;
                stats.customersServed++;
                stats.totalWaitingTime  += d.waitingTime();
                stats.totalServiceTime  += d.serviceDuration;
                stats.totalTimeInSystem += d.totalTimeInSystem();
                servers[s].isBusy=false;
                servers[s].customersServed++;
                servers[s].totalBusyTicks+=d.serviceDuration;
                slotBusy[s]=false;
                addLog(std::string(GREY)+"[T"+padL(std::to_string(clock),3)+"] "
                      +RESET+GREENBOLD+"DEPART "+RESET
                      +"Customer #"+std::to_string(d.id)
                      +GREY+" from Srv-"+std::to_string(s+1)+RESET);
            }
        }

        // 3 Assign queue → free servers
        for (int s=0;s<sc.numServers;s++){
            if(!slotBusy[s]&&!waitQ.isEmpty()){
                Customer c=waitQ.dequeue();
                c.serviceStartTime=clock;
                servers[s].isBusy=true;
                servers[s].availableAt=clock+c.serviceDuration;
                active[s]=c; slotBusy[s]=true;
                addLog(std::string(GREY)+"[T"+padL(std::to_string(clock),3)+"] "
                      +RESET+AMBER+"SERVE  "+RESET
                      +"Customer #"+std::to_string(c.id)
                      +GREY+" → Srv-"+std::to_string(s+1)+RESET);
            }
        }

        // 4 Stats snapshot
        int qLen=waitQ.size();
        stats.recordQueueLength(qLen, clock);
        qHist.push_back(qLen);
        if((int)qHist.size()>HIST) qHist.erase(qHist.begin());

        // 5. Draw
        bool draw = (cfg.speedMs>0)||(clock%5==0)||(clock==sc.simulationTime-1);
        if (!draw) continue;

        moveTo(1,1);

        // Sepreator
        std::cout
            << AMBERBOLD
            << " \n ════════════════════════════════════════════════════════ \n"
            << RESET;

        // Progress bar
        int prog=(int)(50.0*clock/sc.simulationTime);
        std::cout << "\n  " GREY "Tick " RESET
                  << AMBERBOLD << padL(std::to_string(clock),4) << RESET
                  << GREY " / " RESET << sc.simulationTime << "  "
                  << GREY "[" RESET << GREENBOLD << repeat('#',prog) << RESET
                  << GREY << repeat('.',50-prog) << "]\n\n" RESET;

        // Stats row
        auto sbox = [](const std::string& lbl, const std::string& val, const char* col){
            return std::string("  ")
                 + GREY + padR(lbl,14) + RESET
                 + col  + padL(val,8)  + RESET + "   ";
        };
        std::cout
            << sbox("Arrived",   std::to_string(stats.totalCustomers),  CYANBOLD)
            << sbox("Served",    std::to_string(stats.customersServed),  GREENBOLD)
            << sbox("In Queue",  std::to_string(qLen),                   AMBERBOLD)
            << "\n"
            << sbox("Avg Wait",  stats.customersServed?fmtF(stats.avgWaitingTime())+"t":"—", RED)
            << sbox("Avg Q Len", fmtF(stats.avgQueueLength()),            AMBER)
            << sbox("Throughput",fmtF(stats.throughput(),3),              CYAN)
            << "\n\n";

        // Queue track
        std::cout << "  " AMBERBOLD "WAIT QUEUE" RESET "  ▶  ";
        // Snapshot queue contents without toArray (Queue.h doesn't expose it)
        std::vector<Customer> qArr;
        {
            Queue<Customer> tmp=waitQ;
            while(!tmp.isEmpty()) qArr.push_back(tmp.dequeue());
        }
        std::cout << chips(qArr,14) << "\n\n";

        // Servers
        std::cout << "  " AMBERBOLD "SERVERS\n" RESET;
        for (int s=0;s<sc.numServers;s++){
            int prog2=0; std::string info="(idle)";
            if(slotBusy[s]){
                int el=clock-active[s].serviceStartTime;
                prog2=std::min(100,(int)(100.0*el/active[s].serviceDuration));
                info="C#"+std::to_string(active[s].id)
                    +"  "+std::to_string(el)+"/"+std::to_string(active[s].serviceDuration)+"t";
            }
            int bf=prog2*20/100;
            std::cout << "  " GREY "Srv-" RESET
                      << AMBERBOLD << (s+1) << RESET << "  "
                      << (slotBusy[s]?std::string(GREENBOLD)+"● SERVING":std::string(GREY)+"○ IDLE   ")
                      << RESET << "  " << vpad(info,20)
                      << "  " GREY "[" RESET << GREENBOLD << repeat('#',bf) << RESET
                      << GREY << repeat('.',20-bf) << "]" RESET
                      << "  " << padL(std::to_string(prog2),3) << "%\n";
        }
        std::cout << "\n";

        // Chart
        std::cout << "  " AMBERBOLD "QUEUE LENGTH — LAST " << std::min((int)qHist.size(),HIST)
                  << " TICKS\n" RESET;
        drawBarChart(qHist, HIST, CHART_H);
        std::cout << "\n";

        // Log
        std::cout << "  " AMBERBOLD "EVENT LOG\n" RESET;
        for (int l=0;l<LOG_N;l++){
            if (l<(int)elog.size()) std::cout << "  " << elog[l];
            std::cout << "\033[K\n";
        }

        std::cout.flush();

        if (cfg.speedMs > 0) {
            Sleep(cfg.speedMs);
        }
    }

    // ── Final report ──
    std::cout << "\n\n"
        AMBERBOLD
        "  ╔══════════════════════════════════════════════════════╗\n"
        "  ║                  FINAL RESULTS                       ║\n"
        "  ╚══════════════════════════════════════════════════════╝\n"
        RESET "\n"
        << GREY "  Config: " RESET
        << sc.numServers << " server(s)  │  ~1/" << sc.arrivalRate << " ticks  │  "
        << "svc[" << sc.minServiceTime << "-" << sc.maxServiceTime << "]t  │  "
        << sc.simulationTime << " ticks\n\n"
        << GREY "  ┌─────────────────────────────┬───────────────┐\n"
              "  │ Metric                      │ Value         │\n"
              "  ├─────────────────────────────┼───────────────┤\n" RESET;

    auto row=[](const std::string& k,const std::string& v,const char* c){
        std::cout << GREY "  │ " RESET << padR(k,27) << GREY "│ " RESET
                  << c << padR(v,13) << RESET << GREY "│\n" RESET;
    };
    row("Customers Arrived",  std::to_string(stats.totalCustomers),          CYANBOLD);
    row("Customers Served",   std::to_string(stats.customersServed),          GREENBOLD);
    row("Remaining in Queue", std::to_string(waitQ.size()),                   AMBERBOLD);
    row("Avg Waiting Time",   fmtF(stats.avgWaitingTime())+" ticks",          RED);
    row("Avg Service Time",   fmtF(stats.avgServiceTime())+" ticks",          GREY);
    row("Avg Time in System", fmtF(stats.avgTimeInSystem())+" ticks",         GREY);
    row("Avg Queue Length",   fmtF(stats.avgQueueLength()),                   AMBER);
    row("Max Queue Length",   std::to_string(stats.maxQueueLength),           AMBERBOLD);
    row("Throughput",         fmtF(stats.throughput(),4)+" cust/tick",        CYAN);
    std::cout << GREY "  └─────────────────────────────┴───────────────┘\n" RESET "\n";

    std::cout << AMBERBOLD "  SERVER UTILISATION\n" RESET;
    for (int s=0;s<sc.numServers;s++){
        double u=sc.simulationTime>0?(double)servers[s].totalBusyTicks/sc.simulationTime*100:0;
        int bf=(int)(u/100*30);
        const char* c=u<60?GREEN:(u<90?AMBER:RED);
        std::cout << "  " GREY "Srv-" RESET << AMBERBOLD << (s+1) << RESET << "  "
                  << c << "[" << repeat('#',bf) << GREY << repeat('.',30-bf)
                  << c << "]  " << fmtF(u) << "%" RESET
                  << GREY "  (" << servers[s].customersServed << " served)\n" RESET;
    }
    std::cout << "\n";
}

// ── main function
int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::signal(SIGINT, onExit);
    Config cfg = promptConfig();
    clearScreen(); moveTo(1,1);
    runScenarios();
    std::cout << "\n" GREY "  Press ENTER to start live simulation..." RESET;
    std::string tmp; std::getline(std::cin, tmp);
    runLiveSim(cfg);
    std::cout << GREY "\n  Press ENTER to exit..." RESET;
    std::getline(std::cin, tmp);
    showCursor();
    return 0;
}