/*
 * simulator_bridge.cpp
 *
 * Thin CLI wrapper around Simulator.h / Models.h / Queue.h / Stats.h.
 * All business logic lives in those headers — this file only handles
 * JSON I/O so Python can drive everything via subprocess.
 *
 * Protocol
 * --------
 * Python writes ONE line of JSON to stdin, then reads ONE line of JSON
 * from stdout per request.
 *
 * Request shapes
 * --------------
 * { "cmd": "run",
 *   "numServers": 2, "arrivalRate": 3,
 *   "minServiceTime": 2, "maxServiceTime": 6,
 *   "simulationTime": 100, "randomSeed": 42 }
 *
 * { "cmd": "scenarios" }
 *
 * { "cmd": "quit" }
 *
 * Response shapes (one JSON line each)
 * -------------------------------------
 * run      → { "ok":true, "totalCustomers":…, "customersServed":…,
 *              "avgWaitingTime":…, "avgServiceTime":…,
 *              "avgTimeInSystem":…, "avgQueueLength":…,
 *              "maxQueueLength":…, "throughput":…,
 *              "simulationDuration":…,
 *              "queueHistory":[…] }           ← per-tick lengths
 *
 * scenarios → { "ok":true, "scenarios":[ { "name":…, same stats fields … }, … ] }
 *
 * error    → { "ok":false, "error":"…" }
 */

#include "Simulator.h"   // pulls in Queue.h, Models.h, Stats.h

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>

// ── Minimal JSON helpers ─────────────────────────────────────────────────────

// Escape a string for JSON output (handles quotes and backslashes)
static std::string jsonStr(const std::string& s) {
    std::string out; out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    out += '"';
    return out;
}

static std::string fmtDouble(double v, int dp = 4) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", dp, v);
    return buf;
}

// Serialize the common stats fields (shared by both run and scenarios)
static std::string statsJson(const SimStats& st) {
    std::ostringstream o;
    o << "\"totalCustomers\":"    << st.totalCustomers     << ","
      << "\"customersServed\":"   << st.customersServed    << ","
      << "\"avgWaitingTime\":"    << fmtDouble(st.avgWaitingTime())   << ","
      << "\"avgServiceTime\":"    << fmtDouble(st.avgServiceTime())   << ","
      << "\"avgTimeInSystem\":"   << fmtDouble(st.avgTimeInSystem())  << ","
      << "\"avgQueueLength\":"    << fmtDouble(st.avgQueueLength())   << ","
      << "\"maxQueueLength\":"    << st.maxQueueLength                << ","
      << "\"throughput\":"        << fmtDouble(st.throughput())       << ","
      << "\"simulationDuration\":" << st.simulationDuration;
    return o.str();
}

// Serialize the per-tick queue length history
static std::string historyJson(const std::vector<int>& h) {
    std::ostringstream o;
    o << "[";
    for (std::size_t i = 0; i < h.size(); ++i) {
        if (i) o << ",";
        o << h[i];
    }
    o << "]";
    return o.str();
}

// ── Tiny JSON parser (handles only the flat key:value objects we send) ───────

struct KV { std::string key, val; };

// Returns token (strips leading whitespace, stops at delimiter)
static std::string nextToken(const std::string& s, std::size_t& pos,
                              const std::string& delims) {
    while (pos < s.size() && (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r'))
        ++pos;
    if (pos >= s.size()) return "";

    if (s[pos] == '"') {
        ++pos;
        std::string tok;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos]=='\\' && pos+1<s.size()) { ++pos; }
            tok += s[pos++];
        }
        if (pos < s.size()) ++pos; // closing quote
        return tok;
    }
    // bare value until delimiter
    std::string tok;
    while (pos < s.size() && delims.find(s[pos]) == std::string::npos)
        tok += s[pos++];
    // trim trailing whitespace
    while (!tok.empty() && (tok.back()==' '||tok.back()=='\t')) tok.pop_back();
    return tok;
}

static std::vector<KV> parseJson(const std::string& line) {
    std::vector<KV> result;
    std::size_t pos = 0;
    // skip opening '{'
    while (pos < line.size() && line[pos] != '{') ++pos;
    if (pos < line.size()) ++pos;

    while (pos < line.size()) {
        while (pos<line.size() && (line[pos]==' '||line[pos]=='\t'||
                                    line[pos]=='\n'||line[pos]=='\r'||
                                    line[pos]==',')) ++pos;
        if (pos >= line.size() || line[pos]=='}') break;

        std::string key = nextToken(line, pos, ":");
        while (pos<line.size() && line[pos]!=':') ++pos;
        if (pos<line.size()) ++pos; // skip ':'
        std::string val = nextToken(line, pos, ",}");
        if (!key.empty()) result.push_back({key, val});
    }
    return result;
}

static std::string getVal(const std::vector<KV>& kvs, const std::string& key,
                           const std::string& def = "") {
    for (auto& kv : kvs) if (kv.key == key) return kv.val;
    return def;
}
static int getInt(const std::vector<KV>& kvs, const std::string& key, int def = 0) {
    auto v = getVal(kvs, key, "");
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

// ── Command handlers ─────────────────────────────────────────────────────────

static std::string handleRun(const std::vector<KV>& kvs) {
    SimConfig cfg;
    cfg.numServers      = getInt(kvs, "numServers",      2);
    cfg.arrivalRate     = getInt(kvs, "arrivalRate",     3);
    cfg.minServiceTime  = getInt(kvs, "minServiceTime",  2);
    cfg.maxServiceTime  = getInt(kvs, "maxServiceTime",  6);
    cfg.simulationTime  = getInt(kvs, "simulationTime",  100);
    cfg.randomSeed      = getInt(kvs, "randomSeed",      42);

    try {
        Simulator sim(cfg);
        SimStats  st = sim.run();
        std::ostringstream o;
        o << "{\"ok\":true,"
          << statsJson(st) << ","
          << "\"queueHistory\":" << historyJson(st.queueLengthHistory)
          << "}";
        return o.str();
    } catch (const std::exception& e) {
        return std::string("{\"ok\":false,\"error\":") + jsonStr(e.what()) + "}";
    }
}

static std::string handleScenarios() {
    struct Sc { std::string name; SimConfig cfg; };
    std::vector<Sc> list = {
        {"1 Server | Slow arrivals",  {1,5,2,4,200,42}},
        {"1 Server | Fast arrivals",  {1,2,2,4,200,42}},
        {"2 Servers | Fast arrivals", {2,2,2,4,200,42}},
        {"3 Servers | Fast arrivals", {3,2,2,4,200,42}},
        {"2 Servers | Long service",  {2,3,5,10,200,42}},
        {"4 Servers | Long service",  {4,3,5,10,200,42}},
    };

    std::ostringstream o;
    o << "{\"ok\":true,\"scenarios\":[";
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i) o << ",";
        Simulator sim(list[i].cfg);
        SimStats  st = sim.run();
        o << "{"
          << "\"name\":"       << jsonStr(list[i].name) << ","
          << "\"numServers\":" << list[i].cfg.numServers << ","
          << statsJson(st)
          << "}";
    }
    o << "]}";
    return o.str();
}

// ── Entry point ──────────────────────────────────────────────────────────────

int main() {
    // Disable buffering so Python gets responses immediately
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout << std::unitbuf; // flush after every insertion

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        auto kvs = parseJson(line);
        std::string cmd = getVal(kvs, "cmd", "");

        std::string response;
        if (cmd == "quit") {
            std::cout << "{\"ok\":true,\"msg\":\"bye\"}\n";
            break;
        } else if (cmd == "run") {
            response = handleRun(kvs);
        } else if (cmd == "scenarios") {
            response = handleScenarios();
        } else {
            response = std::string("{\"ok\":false,\"error\":") +
                       jsonStr("unknown command: " + cmd) + "}";
        }

        std::cout << response << "\n";
    }
    return 0;
}
