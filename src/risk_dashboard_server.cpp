#include "monte_carlo_engine.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::system_clock;

std::string trimCopy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

bool parseDouble(const std::string& text, double& value) {
    const std::string cleaned = trimCopy(text);
    if (cleaned.empty()) return false;
    try {
        std::size_t idx = 0;
        value = std::stod(cleaned, &idx);
        return idx == cleaned.size();
    } catch (...) {
        return false;
    }
}

bool parseLong(const std::string& text, long& value) {
    const std::string cleaned = trimCopy(text);
    if (cleaned.empty()) return false;
    try {
        std::size_t idx = 0;
        value = std::stol(cleaned, &idx);
        return idx == cleaned.size();
    } catch (...) {
        return false;
    }
}

std::string urlDecode(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '%' && i + 2 < input.size()) {
            const char hi = input[i + 1];
            const char lo = input[i + 2];
            auto hex = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
                if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
                return -1;
            };
            const int h = hex(hi);
            const int l = hex(lo);
            if (h >= 0 && l >= 0) {
                result.push_back(static_cast<char>((h << 4) | l));
                i += 2;
                continue;
            }
        }
        if (c == '+') {
            result.push_back(' ');
        } else {
            result.push_back(c);
        }
    }
    return result;
}

std::unordered_map<std::string, std::string> parseQuery(const std::string& query) {
    std::unordered_map<std::string, std::string> params;
    std::size_t start = 0;
    while (start < query.size()) {
        const std::size_t end = query.find('&', start);
        const std::size_t eq = query.find('=', start);
        if (eq != std::string::npos && (end == std::string::npos || eq < end)) {
            const std::string key = urlDecode(std::string_view(query).substr(start, eq - start));
            const std::string value = urlDecode(
                std::string_view(query).substr(eq + 1, (end == std::string::npos ? query.size() : end) - eq - 1));
            params[key] = value;
        } else {
            const std::string key = urlDecode(
                std::string_view(query).substr(start, (end == std::string::npos ? query.size() : end) - start));
            params[key] = "";
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return params;
}

std::string isoTimestamp(const Clock::time_point tp) {
    const std::time_t time = Clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&time, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

struct HistoricalPoint {
    std::string symbol;
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double adjustedClose = 0.0;
    long volume = 0;
};

class HistoricalStore {
public:
    HistoricalStore() = default;
    HistoricalStore(const HistoricalStore&) = delete;
    HistoricalStore& operator=(const HistoricalStore&) = delete;

    HistoricalStore(HistoricalStore&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        data_ = std::move(other.data_);
        symbol_ = std::move(other.symbol_);
    }

    HistoricalStore& operator=(HistoricalStore&& other) noexcept {
        if (this != &other) {
            std::scoped_lock guard(mutex_, other.mutex_);
            data_ = std::move(other.data_);
            symbol_ = std::move(other.symbol_);
        }
        return *this;
    }

    void loadFromCsv(const std::string& symbol, const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Unable to open historical CSV: " + path);
        }
        std::string header;
        if (!std::getline(file, header)) {
            throw std::runtime_error("CSV appears empty: " + path);
        }

        std::string line;
        std::lock_guard guard(mutex_);
        data_.clear();
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string cell;
            HistoricalPoint point;
            point.symbol = symbol;

            if (!std::getline(ss, cell, ',')) continue;
            point.date = trimCopy(cell);
            if (point.date.empty()) continue;

            double value = 0.0;
            if (!std::getline(ss, cell, ',')) continue;
            if (!parseDouble(cell, value)) continue;
            point.open = value;

            if (!std::getline(ss, cell, ',')) continue;
            if (!parseDouble(cell, value)) continue;
            point.high = value;

            if (!std::getline(ss, cell, ',')) continue;
            if (!parseDouble(cell, value)) continue;
            point.low = value;

            if (!std::getline(ss, cell, ',')) continue;
            if (!parseDouble(cell, value)) continue;
            point.close = value;

            if (!std::getline(ss, cell, ',')) continue;
            if (!parseDouble(cell, value)) {
                point.adjustedClose = point.close;
            } else {
                point.adjustedClose = value;
            }

            long volumeValue = 0;
            if (std::getline(ss, cell, ',')) {
                if (parseLong(cell, volumeValue)) {
                    point.volume = volumeValue;
                } else {
                    point.volume = 0;
                }
            }

            data_.push_back(point);
        }
        symbol_ = symbol;
    }

    [[nodiscard]] std::vector<HistoricalPoint> latest(std::size_t count) const {
        std::lock_guard guard(mutex_);
        if (data_.empty()) return {};
        const std::size_t begin = data_.size() > count ? data_.size() - count : 0;
        return {data_.begin() + static_cast<std::ptrdiff_t>(begin), data_.end()};
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard guard(mutex_);
        return data_.empty();
    }

    [[nodiscard]] std::string symbol() const {
        std::lock_guard guard(mutex_);
        return symbol_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<HistoricalPoint> data_;
    std::string symbol_;
};

struct SimulationRecord {
    std::string command;
    std::string timestamp;
    double durationSeconds = 0.0;
    int threadCount = 1;
    std::size_t samplesProcessed = 0;  // number of simulated paths
    double throughputPerSec = 0.0;     // paths per second for quick diagnostics
    MarketParams market;
    SimulationConfig simulation;
    OptionConfig optionConfig;
    OptionResult optionResult;
    VaRConfig varConfig;
    VaRResult varResult;
};

class SimulationLedger {
public:
    explicit SimulationLedger(std::size_t maxRecords) : maxRecords_(maxRecords) {}

    void push(SimulationRecord record) {
        std::lock_guard guard(mutex_);
        records_.push_front(std::move(record));
        while (records_.size() > maxRecords_) {
            records_.pop_back();
        }
    }

    [[nodiscard]] std::vector<SimulationRecord> snapshot() const {
        std::lock_guard guard(mutex_);
        return {records_.begin(), records_.end()};
    }

private:
    const std::size_t maxRecords_;
    mutable std::mutex mutex_;
    std::deque<SimulationRecord> records_;
};

struct ParsedRequest {
    std::string method;
    std::string path;
    std::string query;
};

std::optional<ParsedRequest> parseRequestLine(const std::string& request) {
    const std::size_t endLine = request.find("\r\n");
    if (endLine == std::string::npos) {
        return std::nullopt;
    }
    std::istringstream line(request.substr(0, endLine));
    ParsedRequest parsed;
    if (!(line >> parsed.method)) return std::nullopt;

    std::string target;
    if (!(line >> target)) return std::nullopt;

    const std::size_t qpos = target.find('?');
    if (qpos != std::string::npos) {
        parsed.path = target.substr(0, qpos);
        parsed.query = target.substr(qpos + 1);
    } else {
        parsed.path = target;
    }
    return parsed;
}

std::string httpResponse(const std::string& body,
                         const std::string& contentType = "text/html",
                         int status = 200,
                         const std::string& statusText = "OK") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << ' ' << statusText << "\r\n"
        << "Content-Type: " << contentType << "; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string toJson(const SimulationRecord& rec) {
    std::ostringstream oss;
    oss << "{"
        << "\"command\":\"" << rec.command << "\"," 
        << "\"timestamp\":\"" << rec.timestamp << "\"," 
        << "\"durationSeconds\":" << rec.durationSeconds << ","
        << "\"threadCount\":" << rec.threadCount;
    if (rec.samplesProcessed > 0) {
        oss << ",\"samplesProcessed\":" << rec.samplesProcessed;
    }
    if (rec.throughputPerSec > 0.0) {
        oss << ",\"throughputPerSec\":" << rec.throughputPerSec;
    }
    if (rec.command == "option") {
        oss << ",\"result\":{"
            << "\"price\":" << rec.optionResult.price << ","
            << "\"standardError\":" << rec.optionResult.standardError << ","
            << "\"analyticPrice\":" << rec.optionResult.analyticPrice << ","
            << "\"relativeError\":" << rec.optionResult.relativeError << ","
            << "\"controlVariateWeight\":" << rec.optionResult.controlVariateWeight << "}"
            << ",\"input\":{"
            << "\"spot\":" << rec.market.spot << ","
            << "\"strike\":" << rec.optionConfig.strike << ","
            << "\"isCall\":" << (rec.optionConfig.isCall ? "true" : "false") << ","
            << "\"paths\":" << rec.simulation.paths << "}";
    } else if (rec.command == "var") {
        oss << ",\"result\":{"
            << "\"valueAtRisk\":" << rec.varResult.valueAtRisk << ","
            << "\"expectedShortfall\":" << rec.varResult.expectedShortfall << ","
            << "\"meanLoss\":" << rec.varResult.meanLoss << ","
            << "\"lossStdDev\":" << rec.varResult.lossStdDev << "}"
            << ",\"input\":{"
            << "\"spot\":" << rec.market.spot << ","
            << "\"percentile\":" << rec.varConfig.percentile << ","
            << "\"notional\":" << rec.varConfig.notional << ","
            << "\"paths\":" << rec.simulation.paths << "}";
    }
    oss << "}";
    return oss.str();
}

std::string toJson(const std::vector<SimulationRecord>& records) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < records.size(); ++i) {
        oss << toJson(records[i]);
        if (i + 1 < records.size()) {
            oss << ",";
        }
    }
    oss << "]";
    return oss.str();
}

std::string toJson(const std::vector<HistoricalPoint>& points) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& pt = points[i];
        oss << "{"
            << "\"symbol\":\"" << pt.symbol << "\","
            << "\"date\":\"" << pt.date << "\","
            << "\"open\":" << pt.open << ","
            << "\"high\":" << pt.high << ","
            << "\"low\":" << pt.low << ","
            << "\"close\":" << pt.close << ","
            << "\"adjustedClose\":" << pt.adjustedClose << ","
            << "\"volume\":" << pt.volume
            << "}";
        if (i + 1 < points.size()) {
            oss << ",";
        }
    }
    oss << "]";
    return oss.str();
}

double getDouble(const std::unordered_map<std::string, std::string>& params,
                 const std::string& key,
                 double fallback) {
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    try {
        return std::stod(it->second);
    } catch (...) {
        return fallback;
    }
}

std::size_t getSize(const std::unordered_map<std::string, std::string>& params,
                    const std::string& key,
                    std::size_t fallback) {
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    try {
        return static_cast<std::size_t>(std::stoull(it->second));
    } catch (...) {
        return fallback;
    }
}

bool getBool(const std::unordered_map<std::string, std::string>& params,
             const std::string& key,
             bool fallback) {
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    if (value == "true" || value == "1" || value == "yes") return true;
    if (value == "false" || value == "0" || value == "no") return false;
    return fallback;
}

struct ServerConfig {
    int port = 8080;
    std::size_t maxRecords = 128;
    std::optional<std::string> historicalSymbol;
    std::optional<std::string> historicalPath;
    std::optional<std::filesystem::path> staticRoot;
    std::optional<std::filesystem::path> dataStore;
};

ServerConfig parseArgs(int argc, char** argv) {
    ServerConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            cfg.port = std::stoi(argv[++i]);
        } else if (arg == "--max-records" && i + 1 < argc) {
            cfg.maxRecords = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--historical-symbol" && i + 1 < argc) {
            cfg.historicalSymbol = argv[++i];
        } else if (arg == "--historical-csv" && i + 1 < argc) {
            cfg.historicalPath = argv[++i];
        } else if (arg == "--static-root" && i + 1 < argc) {
            cfg.staticRoot = std::filesystem::path(argv[++i]);
        } else if (arg == "--data-store" && i + 1 < argc) {
            cfg.dataStore = std::filesystem::path(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: risk_dashboard [--port N] [--max-records N] "
                         "[--historical-symbol SYM --historical-csv PATH] "
                         "[--static-root PATH] [--data-store FILE]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }
    return cfg;
}

int createListeningSocket(int port) {
    const int serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(serverFd);
        throw std::runtime_error("setsockopt failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(serverFd);
        throw std::runtime_error("bind failed (port in use?)");
    }

    if (listen(serverFd, SOMAXCONN) < 0) {
        ::close(serverFd);
        throw std::runtime_error("listen failed");
    }

    return serverFd;
}

class DashboardServer {
public:
    DashboardServer(ServerConfig cfg, HistoricalStore store)
        : config_(std::move(cfg)),
          ledger_(config_.maxRecords),
          historical_(std::move(store)),
          staticRoot_(config_.staticRoot),
          dataStore_(config_.dataStore),
          serverFd_(createListeningSocket(config_.port)) {
        if (dataStore_) {
            if (dataStore_->has_parent_path() && !dataStore_->parent_path().empty()) {
                std::error_code ec;
                std::filesystem::create_directories(dataStore_->parent_path(), ec);
                if (ec) {
                    throw std::runtime_error("Failed to create data-store directory: " + ec.message());
                }
            }
        }
    }

    ~DashboardServer() {
        stop();
    }

    void run() {
        std::cout << "[risk_dashboard] listening on port " << config_.port << std::endl;
        running_.store(true);
        while (running_.load()) {
            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            const int clientFd = ::accept(serverFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
            if (clientFd < 0) {
                if (errno == EINTR) continue;
                std::perror("accept");
                continue;
            }
            std::thread(&DashboardServer::serveClient, this, clientFd).detach();
        }
    }

    void stop() {
        running_.store(false);
        if (serverFd_ >= 0) {
            ::close(serverFd_);
            serverFd_ = -1;
        }
    }

private:
    static std::string contentTypeFor(const std::filesystem::path& file) {
        const std::string ext = file.extension().string();
        if (ext == ".html") return "text/html";
        if (ext == ".js") return "application/javascript";
        if (ext == ".css") return "text/css";
        if (ext == ".json") return "application/json";
        if (ext == ".svg") return "image/svg+xml";
        if (ext == ".png") return "image/png";
        if (ext == ".ico") return "image/x-icon";
        return "application/octet-stream";
    }

    bool serveStatic(int clientFd, const std::string& requestPath) {
        if (!staticRoot_) return false;

        std::filesystem::path resolved = *staticRoot_;

        std::string trimmed = requestPath;
        if (!trimmed.empty() && trimmed.front() == '/') {
            trimmed.erase(trimmed.begin());
        }

        std::filesystem::path relative = trimmed.empty()
                                             ? std::filesystem::path("index.html")
                                             : std::filesystem::path(trimmed).relative_path();

        // Normalize and prevent path traversal.
        std::filesystem::path sanitized;
        for (const auto& part : relative) {
            if (part == ".." || part == ".") {
                continue;
            }
            sanitized /= part;
        }

        resolved /= sanitized;

        if (std::filesystem::is_directory(resolved)) {
            resolved /= "index.html";
        }

        if (!std::filesystem::exists(resolved)) {
            if (requestPath != "/" && staticRoot_) {
                // SPA fallback to index.html
                resolved = *staticRoot_ / "index.html";
                if (!std::filesystem::exists(resolved)) {
                    return false;
                }
            } else {
                return false;
            }
        }

        std::ifstream file(resolved, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        std::ostringstream data;
        data << file.rdbuf();

        const std::string body = data.str();
        const std::string resp = httpResponse(body, contentTypeFor(resolved));
        ::send(clientFd, resp.data(), resp.size(), 0);
        return true;
    }

    void persistRecord(const SimulationRecord& record) {
        if (!dataStore_) return;
        const std::string line = toJson(record);
        std::lock_guard guard(storageMutex_);
        std::ofstream out(*dataStore_, std::ios::app);
        if (!out.is_open()) {
            std::cerr << "[risk_dashboard] warning: unable to open data-store file: " << dataStore_->string()
                      << std::endl;
            return;
        }
        out << line << "\n";
    }

    void serveClient(int clientFd) {
        try {
            std::string request;
            char buffer[4096];
            while (true) {
                const ssize_t received = ::recv(clientFd, buffer, sizeof(buffer), 0);
                if (received <= 0) break;
                request.append(buffer, received);
                if (request.find("\r\n\r\n") != std::string::npos) break;
            }

            if (request.empty()) {
                ::close(clientFd);
                return;
            }

            const auto parsed = parseRequestLine(request);
            if (!parsed) {
                const std::string resp = httpResponse("Bad Request", "text/plain", 400, "Bad Request");
                ::send(clientFd, resp.data(), resp.size(), 0);
                ::close(clientFd);
                return;
            }

            const auto params = parseQuery(parsed->query);

            if (parsed->method != "GET") {
                const std::string resp = httpResponse("Method Not Allowed", "text/plain", 405, "Method Not Allowed");
                ::send(clientFd, resp.data(), resp.size(), 0);
                ::close(clientFd);
                return;
            }

            if (parsed->path == "/api/simulations") {
                const std::string body = toJson(ledger_.snapshot());
                const std::string resp = httpResponse(body, "application/json");
                ::send(clientFd, resp.data(), resp.size(), 0);
            } else if (parsed->path == "/api/historical") {
                if (historical_.empty()) {
                    const std::string resp = httpResponse("[]", "application/json");
                    ::send(clientFd, resp.data(), resp.size(), 0);
                } else {
                    std::size_t limit = getSize(params, "limit", 120);
                    limit = std::max<std::size_t>(10, std::min<std::size_t>(limit, 1000));
                    const std::string body = toJson(historical_.latest(limit));
                    const std::string resp = httpResponse(body, "application/json");
                    ::send(clientFd, resp.data(), resp.size(), 0);
                }
            } else if (parsed->path == "/api/option") {
                handleOption(params, clientFd);
            } else if (parsed->path == "/api/var") {
                handleVaR(params, clientFd);
            } else {
                if (!serveStatic(clientFd, parsed->path)) {
                    const std::string resp = httpResponse("Not Found", "text/plain", 404, "Not Found");
                    ::send(clientFd, resp.data(), resp.size(), 0);
                }
            }

        } catch (const std::exception& ex) {
            const std::string body = std::string("{\"error\":\"") + ex.what() + "\"}";
            const std::string resp = httpResponse(body, "application/json", 500, "Internal Server Error");
            ::send(clientFd, resp.data(), resp.size(), 0);
        }
        ::close(clientFd);
    }

    void handleOption(const std::unordered_map<std::string, std::string>& params, int clientFd) {
        MarketParams market;
        market.spot = getDouble(params, "spot", 100.0);
        market.riskFreeRate = getDouble(params, "rate", 0.02);
        market.dividendYield = getDouble(params, "dividend", 0.01);
        market.volatility = getDouble(params, "vol", 0.2);

        SimulationConfig sim;
        sim.maturity = getDouble(params, "maturity", 1.0);
        sim.timeSteps = getSize(params, "steps", 252);
        sim.paths = getSize(params, "paths", 200'000);
        sim.seed = static_cast<unsigned int>(getSize(params, "seed", 42));
        sim.useAntithetic = getBool(params, "antithetic", true);
        sim.useControlVariate = getBool(params, "control", true);
        sim.blockSize = getSize(params, "block", 4096);

        OptionConfig opt;
        opt.strike = getDouble(params, "strike", market.spot);
        const std::string type = [&]() {
            auto it = params.find("type");
            return it == params.end() ? std::string("call") : it->second;
        }();
        opt.isCall = (type != "put");

        const auto start = Clock::now();
        MonteCarloEngine engine(market, sim);
        const OptionResult result = engine.priceEuropeanOption(opt);
        const auto duration = std::chrono::duration<double>(Clock::now() - start).count();

        SimulationRecord record;
        record.command = "option";
        record.timestamp = isoTimestamp(Clock::now());
        record.durationSeconds = duration;
        record.threadCount =
#ifdef _OPENMP
            omp_get_max_threads();
#else
            1;
#endif
        record.samplesProcessed = sim.paths;
        record.throughputPerSec = duration > 0.0 ? static_cast<double>(record.samplesProcessed) / duration : 0.0;
        record.market = market;
        record.simulation = sim;
        record.optionConfig = opt;
        record.optionResult = result;

        ledger_.push(record);
        persistRecord(record);

        std::ostringstream response;
        response << "{"
                 << "\"timestamp\":\"" << record.timestamp << "\","
                 << "\"durationSeconds\":" << record.durationSeconds << ","
                 << "\"threads\":" << record.threadCount << ","
                 << "\"result\":{"
                 << "\"price\":" << result.price << ","
                 << "\"standardError\":" << result.standardError << ","
                 << "\"analyticPrice\":" << result.analyticPrice << ","
                 << "\"relativeError\":" << result.relativeError << ","
                 << "\"controlVariateWeight\":" << result.controlVariateWeight
                 << "}"
                 << "}";

        const std::string resp = httpResponse(response.str(), "application/json");
        ::send(clientFd, resp.data(), resp.size(), 0);
    }

    void handleVaR(const std::unordered_map<std::string, std::string>& params, int clientFd) {
        MarketParams market;
        market.spot = getDouble(params, "spot", 100.0);
        market.riskFreeRate = getDouble(params, "rate", 0.02);
        market.dividendYield = getDouble(params, "dividend", 0.0);
        market.volatility = getDouble(params, "vol", 0.2);

        SimulationConfig sim;
        sim.maturity = getDouble(params, "maturity", 1.0);
        sim.timeSteps = getSize(params, "steps", 252);
        sim.paths = getSize(params, "paths", 200'000);
        sim.seed = static_cast<unsigned int>(getSize(params, "seed", 42));
        sim.useAntithetic = getBool(params, "antithetic", true);
        sim.useControlVariate = getBool(params, "control", false);
        sim.blockSize = getSize(params, "block", 4096);

        VaRConfig varCfg;
        varCfg.notional = getDouble(params, "notional", 1'000'000.0);
        varCfg.percentile = getDouble(params, "percentile", 0.99);

        const auto start = Clock::now();
        MonteCarloEngine engine(market, sim);
        const VaRResult result = engine.computeParametricVaR(varCfg);
        const auto duration = std::chrono::duration<double>(Clock::now() - start).count();

        SimulationRecord record;
        record.command = "var";
        record.timestamp = isoTimestamp(Clock::now());
        record.durationSeconds = duration;
        record.threadCount =
#ifdef _OPENMP
            omp_get_max_threads();
#else
            1;
#endif
        record.samplesProcessed = sim.paths;
        record.throughputPerSec = duration > 0.0 ? static_cast<double>(record.samplesProcessed) / duration : 0.0;
        record.market = market;
        record.simulation = sim;
        record.varConfig = varCfg;
        record.varResult = result;

        ledger_.push(record);
        persistRecord(record);

        std::ostringstream response;
        response << "{"
                 << "\"timestamp\":\"" << record.timestamp << "\","
                 << "\"durationSeconds\":" << record.durationSeconds << ","
                 << "\"threads\":" << record.threadCount << ","
                 << "\"result\":{"
                 << "\"percentile\":" << result.percentile << ","
                 << "\"valueAtRisk\":" << result.valueAtRisk << ","
                 << "\"expectedShortfall\":" << result.expectedShortfall << ","
                 << "\"meanLoss\":" << result.meanLoss << ","
                 << "\"lossStdDev\":" << result.lossStdDev
                 << "}"
                 << "}";

        const std::string resp = httpResponse(response.str(), "application/json");
        ::send(clientFd, resp.data(), resp.size(), 0);
    }

    ServerConfig config_;
    SimulationLedger ledger_;
    HistoricalStore historical_;
    std::optional<std::filesystem::path> staticRoot_;
    std::optional<std::filesystem::path> dataStore_;
    mutable std::mutex storageMutex_;
    std::atomic<bool> running_{false};
    int serverFd_;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        ServerConfig cfg = parseArgs(argc, argv);

        HistoricalStore store;
        if (cfg.historicalSymbol && cfg.historicalPath) {
            store.loadFromCsv(*cfg.historicalSymbol, *cfg.historicalPath);
            std::cout << "[risk_dashboard] loaded historical data for " << *cfg.historicalSymbol << std::endl;
        } else {
            std::cout << "[risk_dashboard] historical data disabled (provide --historical-symbol and --historical-csv)\n";
        }

        DashboardServer server(std::move(cfg), std::move(store));
        server.run();

    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
