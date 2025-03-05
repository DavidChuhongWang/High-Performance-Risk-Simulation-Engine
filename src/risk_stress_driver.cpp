#include "monte_carlo_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

struct StressConfig {
    std::size_t jobs = std::thread::hardware_concurrency();
    std::size_t iterations = 40;
    std::size_t paths = 400'000;
    bool runVar = true;
};

StressConfig parseArgs(int argc, char** argv) {
    StressConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--jobs" && i + 1 < argc) {
            cfg.jobs = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--iterations" && i + 1 < argc) {
            cfg.iterations = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--paths" && i + 1 < argc) {
            cfg.paths = static_cast<std::size_t>(std::stoull(argv[++i]));
        } else if (arg == "--option-only") {
            cfg.runVar = false;
        } else if (arg == "--help") {
            std::cout << "Usage: risk_stress [--jobs N] [--iterations N] [--paths N] [--option-only]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }
    cfg.jobs = std::max<std::size_t>(1, cfg.jobs);
    return cfg;
}

struct OptionStats {
    double price;
    double stdError;
    double analytic;
};

struct VarStats {
    double valueAtRisk;
    double expectedShortfall;
};

struct RunEntry {
    double durationSeconds;
    int threads;
    std::string command;
    std::optional<OptionStats> option;
    std::optional<VarStats> var;
};

double quantile(std::vector<double> values, double q) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double index = q * static_cast<double>(values.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(index);
    const std::size_t hi = std::min(values.size() - 1, lo + 1);
    const double weight = index - static_cast<double>(lo);
    return values[lo] * (1.0 - weight) + values[hi] * weight;
}

template <typename Container>
double mean(const Container& c) {
    if (c.empty()) return 0.0;
    const double total = std::accumulate(c.begin(), c.end(), 0.0);
    return total / static_cast<double>(c.size());
}

template <typename Container>
double stdev(const Container& c) {
    if (c.size() < 2) return 0.0;
    const double mu = mean(c);
    double accum = 0.0;
    for (double v : c) {
        const double diff = v - mu;
        accum += diff * diff;
    }
    return std::sqrt(accum / static_cast<double>(c.size()));
}

void stressWorker(const StressConfig& cfg,
                  std::size_t workerIndex,
                  std::size_t iterations,
                  std::mutex& mutex,
                  std::vector<RunEntry>& results) {
    std::mt19937_64 rng(static_cast<unsigned int>(workerIndex * 7919u + 17u));
    std::uniform_real_distribution<double> strikeDist(80.0, 120.0);
    std::uniform_real_distribution<double> volDist(0.12, 0.4);
    std::uniform_real_distribution<double> maturityDist(0.25, 2.5);
    std::bernoulli_distribution callDist(0.5);
    std::uniform_real_distribution<double> percentileDist(0.95, 0.9975);
    std::uniform_real_distribution<double> notionalDist(5e5, 5e6);

    for (std::size_t i = 0; i < iterations; ++i) {
        // Option simulation
        MarketParams market;
        market.spot = 100.0;
        market.riskFreeRate = 0.02;
        market.dividendYield = 0.01;
        market.volatility = volDist(rng);

        SimulationConfig sim;
        sim.maturity = maturityDist(rng);
        sim.timeSteps = 252;
        sim.paths = cfg.paths;
        sim.seed = static_cast<unsigned int>(rng());
        sim.useAntithetic = true;
        sim.useControlVariate = true;
        sim.blockSize = 4096;

        OptionConfig optionCfg;
        optionCfg.strike = strikeDist(rng);
        optionCfg.isCall = callDist(rng);

        const auto startOpt = std::chrono::steady_clock::now();
        MonteCarloEngine engine(market, sim);
        const OptionResult optionResult = engine.priceEuropeanOption(optionCfg);
        const auto endOpt = std::chrono::steady_clock::now();

        RunEntry optionEntry;
        optionEntry.command = "option";
        optionEntry.durationSeconds = std::chrono::duration<double>(endOpt - startOpt).count();
        optionEntry.threads =
#ifdef _OPENMP
            omp_get_max_threads();
#else
            1;
#endif
        optionEntry.option = OptionStats{optionResult.price, optionResult.standardError, optionResult.analyticPrice};

        {
            std::lock_guard guard(mutex);
            results.push_back(optionEntry);
        }

        if (!cfg.runVar) continue;

        VaRConfig varCfg;
        varCfg.notional = notionalDist(rng);
        varCfg.percentile = percentileDist(rng);

        sim.useControlVariate = false;
        sim.useAntithetic = true;
        market.volatility = volDist(rng);

        const auto startVar = std::chrono::steady_clock::now();
        MonteCarloEngine engineVar(market, sim);
        const VaRResult varResult = engineVar.computeParametricVaR(varCfg);
        const auto endVar = std::chrono::steady_clock::now();

        RunEntry varEntry;
        varEntry.command = "var";
        varEntry.durationSeconds = std::chrono::duration<double>(endVar - startVar).count();
        varEntry.threads =
#ifdef _OPENMP
            omp_get_max_threads();
#else
            1;
#endif
        varEntry.var = VarStats{varResult.valueAtRisk, varResult.expectedShortfall};

        {
            std::lock_guard guard(mutex);
            results.push_back(varEntry);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const StressConfig cfg = parseArgs(argc, argv);

        std::cout << "[risk_stress] jobs=" << cfg.jobs << " iterations=" << cfg.iterations
                  << " paths=" << cfg.paths << " runVar=" << std::boolalpha << cfg.runVar << "\n";

        std::vector<std::thread> workers;
        std::vector<RunEntry> results;
        results.reserve(cfg.iterations * (cfg.runVar ? 2 : 1) * cfg.jobs);
        std::mutex mutex;

        const auto start = std::chrono::steady_clock::now();

        for (std::size_t worker = 0; worker < cfg.jobs; ++worker) {
            workers.emplace_back(stressWorker,
                                 std::ref(cfg),
                                 worker,
                                 cfg.iterations,
                                 std::ref(mutex),
                                 std::ref(results));
        }

        for (auto& thread : workers) {
            thread.join();
        }

        const auto end = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(end - start).count();

        std::vector<double> durations;
        durations.reserve(results.size());
        std::vector<double> threads;
        threads.reserve(results.size());

        std::vector<double> optionPrices;
        std::vector<double> optionStdErr;
        std::vector<double> optionAnalytics;

        std::vector<double> varValues;
        std::vector<double> esValues;

        for (const auto& entry : results) {
            durations.push_back(entry.durationSeconds);
            threads.push_back(static_cast<double>(entry.threads));
            if (entry.command == "option" && entry.option) {
                optionPrices.push_back(entry.option->price);
                optionStdErr.push_back(entry.option->stdError);
                optionAnalytics.push_back(entry.option->analytic);
            } else if (entry.command == "var" && entry.var) {
                varValues.push_back(entry.var->valueAtRisk);
                esValues.push_back(entry.var->expectedShortfall);
            }
        }

        std::cout << "\n=== Aggregate Metrics ===\n";
        std::cout << "Total runs        : " << results.size() << "\n";
        std::cout << "Wall-clock        : " << elapsed << " s\n";
        std::cout << "Mean duration     : " << mean(durations) << " s\n";
        std::cout << "Median duration   : " << quantile(durations, 0.5) << " s\n";
        std::cout << "P99 duration      : " << quantile(durations, 0.99) << " s\n";
        std::cout << "Threads (avg)     : " << mean(threads) << "\n";

        if (!optionPrices.empty()) {
            std::cout << "\n--- Option Pricing ---\n";
            std::cout << "Runs              : " << optionPrices.size() << "\n";
            std::cout << "Price mean        : " << mean(optionPrices) << "\n";
            std::cout << "Price stdev       : " << stdev(optionPrices) << "\n";
            std::cout << "StdErr mean       : " << mean(optionStdErr) << "\n";
            std::cout << "Analytic mean     : " << mean(optionAnalytics) << "\n";
        }

        if (!varValues.empty()) {
            std::cout << "\n--- Value-at-Risk ---\n";
            std::cout << "Runs              : " << varValues.size() << "\n";
            std::cout << "VaR mean          : " << mean(varValues) << "\n";
            std::cout << "VaR stdev         : " << stdev(varValues) << "\n";
            std::cout << "ES mean           : " << mean(esValues) << "\n";
        }

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
