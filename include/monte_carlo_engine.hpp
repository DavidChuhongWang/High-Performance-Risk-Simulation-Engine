#pragma once

#include <cstddef>
#include <vector>

struct MarketParams {
    double spot = 0.0;
    double riskFreeRate = 0.0;
    double dividendYield = 0.0;
    double volatility = 0.0;
};

struct SimulationConfig {
    double maturity = 1.0;
    std::size_t timeSteps = 252;
    std::size_t paths = 10000;
    unsigned int seed = 42;
    bool useAntithetic = true;
    bool useControlVariate = true;
    std::size_t blockSize = 4096;
    double varConfidenceLevel = 0.99;
};

struct VaRConfig {
    double percentile = 0.99;
    double notional = 1.0;
};

struct VaRResult {
    double percentile = 0.99;
    double valueAtRisk = 0.0;
    double expectedShortfall = 0.0;
    double meanLoss = 0.0;
    double lossStdDev = 0.0;
    std::size_t scenarios = 0;
};

struct OptionConfig {
    double strike = 1.0;
    bool isCall = true;
};

struct OptionResult {
    double price = 0.0;
    double standardError = 0.0;
    double analyticPrice = 0.0;
    double relativeError = 0.0;
    double controlVariateWeight = 0.0;
    std::size_t scenarios = 0;
};

struct ConvergencePoint {
    std::size_t scenarios = 0;
    double price = 0.0;
    double absoluteError = 0.0;
    double relativeError = 0.0;
    double standardError = 0.0;
};

class MonteCarloEngine {
public:
    MonteCarloEngine(MarketParams market, SimulationConfig sim);

    [[nodiscard]] VaRResult computeParametricVaR(const VaRConfig& cfg) const;
    [[nodiscard]] OptionResult priceEuropeanOption(const OptionConfig& cfg) const;
    [[nodiscard]] std::vector<ConvergencePoint> convergenceStudy(
        const OptionConfig& cfg,
        const std::vector<std::size_t>& sampleSizes) const;

private:
    MarketParams market_;
    SimulationConfig sim_;

    double pathDrift() const;
    double pathDiffusion() const;

    std::vector<double> simulateTerminalPrices(std::size_t basePaths) const;
    double blackScholesPrice(const OptionConfig& cfg) const;
};
