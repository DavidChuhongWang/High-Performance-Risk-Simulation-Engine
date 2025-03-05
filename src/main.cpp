#include "monte_carlo_engine.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

using ArgMap = std::unordered_map<std::string, std::string>;

enum class OutputFormat { Text, Json };

void printUsage(const char* exe) {
    std::cout << "Usage:\n"
              << "  " << exe
              << " <command> [options]\n\n"
              << "Commands:\n"
              << "  option       Price a European option via Monte Carlo\n"
              << "  var          Estimate portfolio VaR and Expected Shortfall\n"
              << "  convergence  Run a convergence study against Black-Scholes\n\n"
              << "Common Options:\n"
              << "  --spot <value>          Spot price (default: 100)\n"
              << "  --rate <value>          Risk-free rate (default: 0.02)\n"
              << "  --dividend <value>      Dividend yield (default: 0.01)\n"
              << "  --vol <value>           Volatility (default: 0.2)\n"
              << "  --maturity <value>      Time to maturity in years (default: 1)\n"
              << "  --steps <value>         Time steps per path (default: 252)\n"
              << "  --paths <value>         Monte Carlo paths (default: 200000)\n"
              << "  --seed <value>          RNG seed (default: 42)\n"
              << "  --antithetic <bool>     Enable antithetic variates (default: true)\n"
              << "  --control <bool>        Enable control variate (default: true)\n"
              << "  --block <value>         Simulation block size (default: 4096)\n\n"
              << "Option Command Options:\n"
              << "  --strike <value>        Strike price (default: 100)\n"
              << "  --type <call|put>       Option type (default: call)\n\n"
              << "VaR Command Options:\n"
              << "  --notional <value>      Portfolio notional (default: 1)\n"
              << "  --percentile <value>    VaR percentile in (0,1) (default: 0.99)\n\n"
              << "Convergence Command Options:\n"
              << "  --samples <list>        Comma-separated path counts\n"
              << "                          (default: 5000,20000,80000,160000)\n";
}

ArgMap parseArgs(int argc, char** argv, int startIndex) {
    ArgMap args;
    for (int i = startIndex; i < argc; ++i) {
        std::string token = argv[i];
        if (token.rfind("--", 0) != 0) {
            throw std::invalid_argument("Unexpected token: " + token);
        }

        token = token.substr(2);
        const auto pos = token.find('=');
        if (pos != std::string::npos) {
            const std::string key = token.substr(0, pos);
            const std::string value = token.substr(pos + 1);
            args[key] = value;
        } else {
            std::string key = token;
            std::string value = "true";
            if (i + 1 < argc) {
                std::string potential = argv[i + 1];
                if (potential.rfind("--", 0) != 0) {
                    value = potential;
                    ++i;
                }
            }
            args[key] = value;
        }
    }
    return args;
}

double getDouble(const ArgMap& args, const std::string& name, double defaultValue) {
    const auto it = args.find(name);
    if (it == args.end()) {
        return defaultValue;
    }
    return std::stod(it->second);
}

std::size_t getSizeT(const ArgMap& args, const std::string& name, std::size_t defaultValue) {
    const auto it = args.find(name);
    if (it == args.end()) {
        return defaultValue;
    }
    return static_cast<std::size_t>(std::stoull(it->second));
}

bool getBool(const ArgMap& args, const std::string& name, bool defaultValue) {
    const auto it = args.find(name);
    if (it == args.end()) {
        return defaultValue;
    }
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    throw std::invalid_argument("Unable to parse boolean flag: " + name + "=" + it->second);
}

std::string getString(const ArgMap& args, const std::string& name, std::string defaultValue) {
    const auto it = args.find(name);
    if (it == args.end()) {
        return defaultValue;
    }
    return it->second;
}

std::vector<std::size_t> parseSampleList(const ArgMap& args,
                                         const std::string& name,
                                         const std::vector<std::size_t>& defaults) {
    const auto it = args.find(name);
    if (it == args.end()) {
        return defaults;
    }
    std::vector<std::size_t> values;
    std::stringstream ss(it->second);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            values.push_back(static_cast<std::size_t>(std::stoull(item)));
        }
    }
    return values;
}

int detectThreads() {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

OutputFormat parseFormat(const ArgMap& args) {
    std::string fmt = getString(args, "format", "text");
    std::transform(fmt.begin(), fmt.end(), fmt.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (fmt == "json") {
        return OutputFormat::Json;
    }
    if (fmt != "text") {
        throw std::invalid_argument("Unsupported format: " + fmt);
    }
    return OutputFormat::Text;
}

void printOptionResult(const OptionResult& res, OutputFormat format, int threadCount) {
    if (format == OutputFormat::Json) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(10);
        oss << "{\n"
            << "  \"command\": \"option\",\n"
            << "  \"threads\": " << threadCount << ",\n"
            << "  \"result\": {\n"
            << "    \"price\": " << res.price << ",\n"
            << "    \"standardError\": " << res.standardError << ",\n"
            << "    \"analyticPrice\": " << res.analyticPrice << ",\n"
            << "    \"relativeError\": " << res.relativeError << ",\n"
            << "    \"controlVariateWeight\": " << res.controlVariateWeight << ",\n"
            << "    \"scenarios\": " << res.scenarios << "\n"
            << "  }\n"
            << "}\n";
        std::cout << oss.str();
        return;
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Monte Carlo price : " << res.price << " (std. error " << res.standardError << ")\n";
    std::cout << "Black-Scholes     : " << res.analyticPrice
              << " (relative error " << res.relativeError * 100.0 << "%)\n";
    std::cout << "Control variate β : " << res.controlVariateWeight << "\n";
    std::cout << "Paths simulated   : " << res.scenarios << "\n";
}

void printVaRResult(const VaRResult& res, OutputFormat format, int threadCount) {
    if (format == OutputFormat::Json) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(10);
        oss << "{\n"
            << "  \"command\": \"var\",\n"
            << "  \"threads\": " << threadCount << ",\n"
            << "  \"result\": {\n"
            << "    \"percentile\": " << res.percentile << ",\n"
            << "    \"valueAtRisk\": " << res.valueAtRisk << ",\n"
            << "    \"expectedShortfall\": " << res.expectedShortfall << ",\n"
            << "    \"meanLoss\": " << res.meanLoss << ",\n"
            << "    \"lossStdDev\": " << res.lossStdDev << ",\n"
            << "    \"scenarios\": " << res.scenarios << "\n"
            << "  }\n"
            << "}\n";
        std::cout << oss.str();
        return;
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Value-at-Risk (" << res.percentile * 100.0 << "%) : " << res.valueAtRisk << "\n";
    std::cout << "Expected Shortfall               : " << res.expectedShortfall << "\n";
    std::cout << "Mean loss / Std Dev              : " << res.meanLoss << " / " << res.lossStdDev << "\n";
    std::cout << "Scenarios                         : " << res.scenarios << "\n";
}

void printConvergence(const std::vector<ConvergencePoint>& points,
                      OutputFormat format,
                      int threadCount) {
    if (format == OutputFormat::Json) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(10);
        oss << "{\n"
            << "  \"command\": \"convergence\",\n"
            << "  \"threads\": " << threadCount << ",\n"
            << "  \"result\": [\n";
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& point = points[i];
            oss << "    {\n"
                << "      \"scenarios\": " << point.scenarios << ",\n"
                << "      \"price\": " << point.price << ",\n"
                << "      \"absoluteError\": " << point.absoluteError << ",\n"
                << "      \"relativeError\": " << point.relativeError << ",\n"
                << "      \"standardError\": " << point.standardError << "\n"
                << "    }";
            if (i + 1 < points.size()) {
                oss << ",";
            }
            oss << "\n";
        }
        oss << "  ]\n"
            << "}\n";
        std::cout << oss.str();
        return;
    }

    if (points.empty()) {
        std::cout << "No convergence points computed.\n";
        return;
    }
    std::cout << std::fixed << std::setprecision(6);
    std::cout << std::setw(12) << "Paths"
              << std::setw(18) << "Price"
              << std::setw(18) << "Abs Error"
              << std::setw(18) << "Rel Error"
              << std::setw(18) << "Std Error"
              << "\n";
    for (const auto& point : points) {
        std::cout << std::setw(12) << point.scenarios
                  << std::setw(18) << point.price
                  << std::setw(18) << point.absoluteError
                  << std::setw(18) << point.relativeError
                  << std::setw(18) << point.standardError
                  << "\n";
    }
}

MarketParams buildMarket(const ArgMap& args) {
    MarketParams market;
    market.spot = getDouble(args, "spot", 100.0);
    market.riskFreeRate = getDouble(args, "rate", 0.02);
    market.dividendYield = getDouble(args, "dividend", 0.01);
    market.volatility = getDouble(args, "vol", 0.2);
    return market;
}

SimulationConfig buildSimulation(const ArgMap& args) {
    SimulationConfig sim;
    sim.maturity = getDouble(args, "maturity", 1.0);
    sim.timeSteps = getSizeT(args, "steps", 252);
    sim.paths = getSizeT(args, "paths", 200000);
    sim.seed = static_cast<unsigned int>(getSizeT(args, "seed", 42));
    sim.useAntithetic = getBool(args, "antithetic", true);
    sim.useControlVariate = getBool(args, "control", true);
    sim.blockSize = getSizeT(args, "block", 4096);
    sim.varConfidenceLevel = getDouble(args, "percentile", 0.99);
    return sim;
}

OptionConfig buildOption(const ArgMap& args, double defaultStrike) {
    OptionConfig cfg;
    cfg.strike = getDouble(args, "strike", defaultStrike);
    std::string type = getString(args, "type", "call");
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (type == "call") {
        cfg.isCall = true;
    } else if (type == "put") {
        cfg.isCall = false;
    } else {
        throw std::invalid_argument("Unknown option type: " + type);
    }
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];

    ArgMap args;
    try {
        args = parseArgs(argc, argv, 2);
    } catch (const std::exception& ex) {
        std::cerr << "Argument error: " << ex.what() << "\n";
        printUsage(argv[0]);
        return 1;
    }

    OutputFormat format = OutputFormat::Text;
    bool formatParsed = false;

    try {
        const MarketParams market = buildMarket(args);
        SimulationConfig sim = buildSimulation(args);
        format = parseFormat(args);
        formatParsed = true;
        const int threads = detectThreads();

        if (format == OutputFormat::Text) {
            std::cout << "High-Performance Risk Simulation Engine\n"
                      << "OpenMP threads: " << threads << "\n\n";
        }

        MonteCarloEngine engine(market, sim);

        if (command == "option") {
            const OptionConfig option = buildOption(args, market.spot);
            const OptionResult res = engine.priceEuropeanOption(option);
            printOptionResult(res, format, threads);
        } else if (command == "var") {
            VaRConfig varCfg;
            varCfg.percentile = getDouble(args, "percentile", sim.varConfidenceLevel);
            varCfg.notional = getDouble(args, "notional", 1.0);
            const VaRResult res = engine.computeParametricVaR(varCfg);
            printVaRResult(res, format, threads);
        } else if (command == "convergence") {
            const OptionConfig option = buildOption(args, market.spot);
            const std::vector<std::size_t> defaults{5000, 20000, 80000, 160000};
            const std::vector<std::size_t> samples = parseSampleList(args, "samples", defaults);
            const std::vector<ConvergencePoint> points = engine.convergenceStudy(option, samples);
            if (format == OutputFormat::Text) {
                std::cout << "Convergence study vs. Black-Scholes analytic price\n";
            }
            printConvergence(points, format, threads);
        } else {
            if (format == OutputFormat::Json) {
                std::cout << "{\n"
                          << "  \"error\": \"Unknown command\",\n"
                          << "  \"details\": \"" << command << "\"\n"
                          << "}\n";
            } else {
                std::cerr << "Unknown command: " << command << "\n\n";
                printUsage(argv[0]);
            }
            return 1;
        }
    } catch (const std::exception& ex) {
        if (formatParsed && format == OutputFormat::Json) {
            std::cout << "{\n"
                      << "  \"error\": \"Runtime\",\n"
                      << "  \"details\": \"" << ex.what() << "\"\n"
                      << "}\n";
            return 1;
        }
        std::cerr << "Runtime error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
