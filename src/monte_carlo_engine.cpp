#include "monte_carlo_engine.hpp"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

constexpr double kEpsilon = 1e-12;

inline double normalCdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

}  // namespace

MonteCarloEngine::MonteCarloEngine(MarketParams market, SimulationConfig sim)
    : market_(std::move(market)), sim_(std::move(sim)) {
    if (sim_.timeSteps == 0) {
        throw std::invalid_argument("SimulationConfig.timeSteps must be positive");
    }
    if (sim_.maturity <= 0.0) {
        throw std::invalid_argument("SimulationConfig.maturity must be positive");
    }
    if (market_.spot <= 0.0) {
        throw std::invalid_argument("MarketParams.spot must be positive");
    }
    if (market_.volatility <= 0.0) {
        throw std::invalid_argument("MarketParams.volatility must be positive");
    }
    if (sim_.paths == 0) {
        throw std::invalid_argument("SimulationConfig.paths must be positive");
    }
    if (sim_.blockSize == 0) {
        sim_.blockSize = 1024;
    }
}

double MonteCarloEngine::pathDrift() const {
    const double dt = sim_.maturity / static_cast<double>(sim_.timeSteps);
    return (market_.riskFreeRate - market_.dividendYield -
            0.5 * market_.volatility * market_.volatility) *
           dt;
}

double MonteCarloEngine::pathDiffusion() const {
    const double dt = sim_.maturity / static_cast<double>(sim_.timeSteps);
    return market_.volatility * std::sqrt(dt);
}

std::vector<double> MonteCarloEngine::simulateTerminalPrices(std::size_t basePaths) const {
    const std::size_t effectivePaths = sim_.useAntithetic ? basePaths * 2 : basePaths;
    std::vector<double> terminal(effectivePaths);

    const double drift = pathDrift();
    const double diffusion = pathDiffusion();

    const std::size_t chunkSize = std::max<std::size_t>(1, sim_.blockSize);

#ifdef _OPENMP
    const int threadCount = omp_get_max_threads();
#else
    const int threadCount = 1;
#endif

#pragma omp parallel
    {
#ifdef _OPENMP
        const int threadId = omp_get_thread_num();
#else
        const int threadId = 0;
#endif
        std::mt19937_64 rng(sim_.seed + 7919u * static_cast<unsigned int>(threadId));
        std::normal_distribution<double> normal(0.0, 1.0);

#pragma omp for schedule(static)
        for (std::size_t start = 0; start < basePaths; start += chunkSize) {
            const std::size_t count = std::min(chunkSize, basePaths - start);
            Eigen::ArrayXd state = Eigen::ArrayXd::Constant(static_cast<Eigen::Index>(count), market_.spot);
            Eigen::ArrayXd antiState;

            if (sim_.useAntithetic) {
                antiState = Eigen::ArrayXd::Constant(static_cast<Eigen::Index>(count), market_.spot);
            }

            Eigen::ArrayXd driftVec =
                Eigen::ArrayXd::Constant(static_cast<Eigen::Index>(count), drift);

            for (std::size_t step = 0; step < sim_.timeSteps; ++step) {
                Eigen::ArrayXd shocks =
                    Eigen::ArrayXd::NullaryExpr(static_cast<Eigen::Index>(count),
                                                [&]() { return normal(rng); });

                const Eigen::ArrayXd evolution =
                    (driftVec + diffusion * shocks).exp();
                state *= evolution;

                if (sim_.useAntithetic) {
                    const Eigen::ArrayXd antiEvolution =
                        (driftVec - diffusion * shocks).exp();
                    antiState *= antiEvolution;
                }
            }

            for (std::size_t i = 0; i < count; ++i) {
                terminal[start + i] = state[static_cast<Eigen::Index>(i)];
            }

            if (sim_.useAntithetic) {
                const std::size_t antiBase = basePaths + start;
                for (std::size_t i = 0; i < count; ++i) {
                    terminal[antiBase + i] = antiState[static_cast<Eigen::Index>(i)];
                }
            }
        }
    }  // omp parallel

    (void)threadCount;  // suppress unused warning when OpenMP is disabled

    return terminal;
}

VaRResult MonteCarloEngine::computeParametricVaR(const VaRConfig& cfg) const {
    if (cfg.percentile <= 0.0 || cfg.percentile >= 1.0) {
        throw std::invalid_argument("VaRConfig.percentile must be in (0, 1)");
    }

    const std::size_t basePaths = sim_.paths;
    const std::vector<double> terminal = simulateTerminalPrices(basePaths);
    const std::size_t totalPaths = terminal.size();

    std::vector<double> losses(totalPaths);
    const double notional = cfg.notional;
    const double invSpot = 1.0 / market_.spot;

#pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < totalPaths; ++i) {
        const double ratio = terminal[i] * invSpot;
        const double pnl = notional * (ratio - 1.0);
        losses[i] = -pnl;
    }

    double sumLoss = 0.0;
    double sumSqLoss = 0.0;

#pragma omp parallel for reduction(+: sumLoss, sumSqLoss) schedule(static)
    for (std::size_t i = 0; i < totalPaths; ++i) {
        sumLoss += losses[i];
        sumSqLoss += losses[i] * losses[i];
    }

    const double meanLoss = sumLoss / static_cast<double>(totalPaths);
    const double variance =
        std::max(0.0, (sumSqLoss / static_cast<double>(totalPaths)) - meanLoss * meanLoss);
    const double stdDevLoss = std::sqrt(variance);

    std::size_t index = static_cast<std::size_t>(
        std::ceil(cfg.percentile * static_cast<double>(totalPaths)));
    if (index == 0) {
        index = 1;
    }
    if (index > totalPaths) {
        index = totalPaths;
    }
    const std::size_t quantileIndex = index - 1;

    std::nth_element(losses.begin(), losses.begin() + static_cast<std::ptrdiff_t>(quantileIndex),
                     losses.end());
    const double var = losses[quantileIndex];

    double tailSum = 0.0;
    std::size_t tailCount = 0;
    for (double loss : losses) {
        if (loss >= var - kEpsilon) {
            tailSum += loss;
            ++tailCount;
        }
    }
    const double expectedShortfall =
        tailCount > 0 ? (tailSum / static_cast<double>(tailCount)) : var;

    VaRResult result;
    result.percentile = cfg.percentile;
    result.valueAtRisk = var;
    result.expectedShortfall = expectedShortfall;
    result.meanLoss = meanLoss;
    result.lossStdDev = stdDevLoss;
    result.scenarios = totalPaths;
    return result;
}

OptionResult MonteCarloEngine::priceEuropeanOption(const OptionConfig& cfg) const {
    if (cfg.strike <= 0.0) {
        throw std::invalid_argument("OptionConfig.strike must be positive");
    }

    const std::size_t basePaths = sim_.paths;

    const double drift = pathDrift();
    const double diffusion = pathDiffusion();
    const double discount = std::exp(-market_.riskFreeRate * sim_.maturity);
    const double expectedControl =
        market_.spot * std::exp(-market_.dividendYield * sim_.maturity);
    const std::size_t chunkSize = std::max<std::size_t>(1, sim_.blockSize);

    double sumPayoff = 0.0;
    double sumSqPayoff = 0.0;
    double sumControl = 0.0;
    double sumSqControl = 0.0;
    double sumCross = 0.0;
    std::size_t count = 0;

#pragma omp parallel
    {
#ifdef _OPENMP
        const int threadId = omp_get_thread_num();
#else
        const int threadId = 0;
#endif
        std::mt19937_64 rng(sim_.seed + 104729u * static_cast<unsigned int>(threadId) + 1337u);
        std::normal_distribution<double> normal(0.0, 1.0);

        double localSumPayoff = 0.0;
        double localSumSqPayoff = 0.0;
        double localSumControl = 0.0;
        double localSumSqControl = 0.0;
        double localSumCross = 0.0;
        std::size_t localCount = 0;

#pragma omp for schedule(static)
        for (std::size_t start = 0; start < basePaths; start += chunkSize) {
            const std::size_t current = std::min(chunkSize, basePaths - start);
            Eigen::ArrayXd state =
                Eigen::ArrayXd::Constant(static_cast<Eigen::Index>(current), market_.spot);
            Eigen::ArrayXd antiState;

            if (sim_.useAntithetic) {
                antiState =
                    Eigen::ArrayXd::Constant(static_cast<Eigen::Index>(current), market_.spot);
            }

            Eigen::ArrayXd driftVec =
                Eigen::ArrayXd::Constant(static_cast<Eigen::Index>(current), drift);

            for (std::size_t step = 0; step < sim_.timeSteps; ++step) {
                Eigen::ArrayXd shocks =
                    Eigen::ArrayXd::NullaryExpr(static_cast<Eigen::Index>(current),
                                                [&]() { return normal(rng); });

                const Eigen::ArrayXd evolution =
                    (driftVec + diffusion * shocks).exp();
                state *= evolution;

                if (sim_.useAntithetic) {
                    const Eigen::ArrayXd antiEvolution =
                        (driftVec - diffusion * shocks).exp();
                    antiState *= antiEvolution;
                }
            }

            for (std::size_t i = 0; i < current; ++i) {
                const double spotT = state[static_cast<Eigen::Index>(i)];
                const double intrinsic = cfg.isCall ? std::max(spotT - cfg.strike, 0.0)
                                                    : std::max(cfg.strike - spotT, 0.0);
                const double payoff = discount * intrinsic;
                const double control = discount * spotT;

                localSumPayoff += payoff;
                localSumSqPayoff += payoff * payoff;
                localSumControl += control;
                localSumSqControl += control * control;
                localSumCross += payoff * control;
                ++localCount;
            }

            if (sim_.useAntithetic) {
                for (std::size_t i = 0; i < current; ++i) {
                    const double spotT = antiState[static_cast<Eigen::Index>(i)];
                    const double intrinsic = cfg.isCall ? std::max(spotT - cfg.strike, 0.0)
                                                        : std::max(cfg.strike - spotT, 0.0);
                    const double payoff = discount * intrinsic;
                    const double control = discount * spotT;

                    localSumPayoff += payoff;
                    localSumSqPayoff += payoff * payoff;
                    localSumControl += control;
                    localSumSqControl += control * control;
                    localSumCross += payoff * control;
                    ++localCount;
                }
            }
        }

#pragma omp atomic
        sumPayoff += localSumPayoff;
#pragma omp atomic
        sumSqPayoff += localSumSqPayoff;
#pragma omp atomic
        sumControl += localSumControl;
#pragma omp atomic
        sumSqControl += localSumSqControl;
#pragma omp atomic
        sumCross += localSumCross;
#pragma omp atomic
        count += localCount;
    }  // omp parallel

    const double invCount = 1.0 / static_cast<double>(count);
    const double meanPayoff = sumPayoff * invCount;
    const double meanControl = sumControl * invCount;
    const double varPayoff =
        std::max(0.0, (sumSqPayoff * invCount) - meanPayoff * meanPayoff);
    const double varControl =
        std::max(0.0, (sumSqControl * invCount) - meanControl * meanControl);
    const double covariance =
        (sumCross * invCount) - meanPayoff * meanControl;

    double beta = 0.0;
    double adjustedMean = meanPayoff;
    double adjustedVariance = varPayoff;

    if (sim_.useControlVariate && varControl > kEpsilon) {
        beta = covariance / varControl;
        adjustedMean = meanPayoff + beta * (expectedControl - meanControl);
        adjustedVariance = varPayoff + beta * beta * varControl - 2.0 * beta * covariance;
        adjustedVariance = std::max(0.0, adjustedVariance);
    }

    const double stdError =
        std::sqrt(adjustedVariance / static_cast<double>(count));
    const double analytic = blackScholesPrice(cfg);
    const double relativeError =
        analytic != 0.0 ? (adjustedMean - analytic) / analytic : 0.0;

    OptionResult result;
    result.price = adjustedMean;
    result.standardError = stdError;
    result.analyticPrice = analytic;
    result.relativeError = relativeError;
    result.controlVariateWeight = beta;
    result.scenarios = count;
    return result;
}

std::vector<ConvergencePoint> MonteCarloEngine::convergenceStudy(
    const OptionConfig& cfg, const std::vector<std::size_t>& sampleSizes) const {
    std::vector<ConvergencePoint> points;
    points.reserve(sampleSizes.size());

    for (std::size_t sample : sampleSizes) {
        SimulationConfig custom = sim_;
        custom.paths = sample;
        MonteCarloEngine engine(market_, custom);
        OptionResult res = engine.priceEuropeanOption(cfg);

        ConvergencePoint pt;
        pt.scenarios = res.scenarios;
        pt.price = res.price;
        pt.absoluteError = std::abs(res.price - res.analyticPrice);
        pt.relativeError = std::abs(res.relativeError);
        pt.standardError = res.standardError;
        points.push_back(pt);
    }

    return points;
}

double MonteCarloEngine::blackScholesPrice(const OptionConfig& cfg) const {
    const double T = sim_.maturity;
    const double sigma = market_.volatility;
    const double r = market_.riskFreeRate;
    const double q = market_.dividendYield;
    const double S = market_.spot;
    const double K = cfg.strike;

    const double sqrtT = std::sqrt(std::max(kEpsilon, T));
    const double sigmaSqrtT = sigma * sqrtT;

    const double logTerm = std::log(S / K);
    const double d1 = (logTerm + (r - q + 0.5 * sigma * sigma) * T) / sigmaSqrtT;
    const double d2 = d1 - sigmaSqrtT;

    const double discDiv = std::exp(-q * T);
    const double discRate = std::exp(-r * T);

    if (cfg.isCall) {
        return S * discDiv * normalCdf(d1) - K * discRate * normalCdf(d2);
    }

    const double put = K * discRate * normalCdf(-d2) - S * discDiv * normalCdf(-d1);
    return put;
}
