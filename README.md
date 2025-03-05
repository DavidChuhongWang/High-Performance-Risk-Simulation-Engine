# High-Performance Risk Simulation Engine

## TL;DR
- **C++20 + Eigen + OpenMP** Monte Carlo engine for option pricing and Value-at-Risk (VaR).
- **`risk_sim` CLI**, **`risk_dashboard` HTTP server**, **`risk_stress` load generator**, and a **React front-end**.
- Python verifier downloads historical market data and cross-checks Monte Carlo output against Black–Scholes and empirical VaR.
- Build with CMake, run the C++ server on port 8080, serve the React UI via Vite (`npm run dev`), and stress-test with `risk_stress`.

---

## Architecture
```
                                  ┌────────────────────────────┐
                                  │  React / Vite Dashboard    │
                                  │  (charts, forms, tuner)    │
                                  └─────────────┬──────────────┘
                                                │ HTTP / JSON REST
                                                ▼
        ┌─────────────────────────────────────────────────────────┐
        │                risk_dashboard (C++ HTTP)                 │
        │  • hosts static front-end assets                        │
        │  • REST: /api/option, /api/var, /api/historical          │
        │  • optional JSONL persistence                            │
        └──────┬───────────────┬────────────────┬─────────────────┘
               │               │                │
               │               │                │ historical prices (CSV)
               │               │                ▼
               │               │      ┌────────────────────────┐
               │               │      │        data/           │
               │               │      │  (SPY.csv, log files)  │
               │               │      └────────────────────────┘
               │               │
               │               │ CLI invocations / benchmarking
               │               │
               │      ┌────────▼──────────┐
               │      │ risk_sim (CLI)    │
               │      │ risk_stress (C++) │
               │      └────────┬──────────┘
               │               │ Monte Carlo API
               ▼               │
        ┌─────────────────────────────────────────────────────────┐
        │            risk_engine (C++20 + Eigen + OpenMP)          │
        │  • GBM path generation with antithetic/control variates  │
        │  • Black–Scholes analytic pricing helpers                │
        └──────┬───────────────────────────────────────────────────┘
               │ samples + metrics
               ▼
        ┌────────────────────────────┐
        │ Python verifier            │
        │ (run_verifier.py)         │
        │ • downloads market data   │
        │ • compares Monte Carlo    │
        │   against analytics       │
        └────────────────────────────┘
```

## Components
- **risk_engine**: GBM stochastic path generator with antithetic pairs, control variate, SIMD-friendly Eigen arrays.
- **risk_sim**: CLI that wraps the engine with text/JSON output. Useful for automation and regression testing.
- **risk_dashboard**: Minimal C++ HTTP server (POSIX sockets) hosting REST endpoints, optional JSONL logging, CSV-backed historical price service, and static front-end files.
- **risk_stress**: Multithreaded benchmark harness that executes randomized simulations and reports latency, throughput, and estimator dispersion.
- **Front-end (`frontend/`)**: React + Vite dashboard featuring simulation forms, performance tuner, throughput/latency charts, workload mix visualisation, and historical price plots (Recharts).
- **Python verifier**: Downloads Yahoo Finance OHLCV data (`yfinance`), runs Monte Carlo, and compares results to analytic and empirical references.

## Prerequisites
- C++20 compiler (Clang ≥16 or GCC ≥12)
- CMake ≥3.18
- Eigen 3.4 headers (`brew install eigen`, `apt install libeigen3-dev`, or `vcpkg install eigen3`)
- Optional: OpenMP 4.0+ for parallel Monte Carlo
- Node.js ≥18 for the React dashboard
- Python 3.10+ with `pip`

## Build the C++ Targets
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Artifacts:
- `build/risk_sim`
- `build/risk_dashboard`
- `build/risk_stress`
- `build/librisk_engine.a`

## Prepare the Front-end
```bash
cd frontend
npm install
npm run build        # writes production assets to frontend/dist
```
> **Tip:** Run all `npm` commands from the `frontend/` directory. To launch the hot-reload dev server instead, use `npm run dev` (see workflow below).

## Run the Dashboard Service
```bash
./build/risk_dashboard \
  --port 8080 \
  --static-root frontend/dist \
  --data-store data/simulation_log.jsonl \
  --historical-symbol SPY \
  --historical-csv data/SPY.csv
```
- JSON responses available via `/api/option`, `/api/var`, `/api/simulations`, `/api/historical`.
- Any other path serves the React build (SPA fallback to `index.html`).
- When running `npm run dev`, Vite proxies `/api/*` to `http://127.0.0.1:8080`, so ensure the C++ server is active or Vite will raise `ECONNREFUSED`.

### Dashboard Features
- Simulation forms mirroring CLI flags for bespoke runs.
- Performance tuner for rapid parameter sweeps with inline JSON previews.
- Throughput (paths/sec) & latency charts over a configurable sliding window.
- Workload mix visualisation (option vs VaR share).
- Historical price chart fed by the CSV supplied to the server.

### Typical Dev Workflow
1. **Start backend** (shell A)
   ```bash
   ./build/risk_dashboard --port 8080 --static-root frontend/dist \
     --data-store data/simulation_log.jsonl \
     --historical-symbol SPY --historical-csv data/SPY.csv
   ```
2. **Run front-end dev server** (shell B)
   ```bash
   cd frontend
   npm run dev
   ```
   Visit `http://localhost:5173` (proxy -> `http://127.0.0.1:8080`).
3. **Production preview**: after `npm run build`, hit `http://localhost:8080` directly.

## Sample Historical Dataset
If you don’t have `data/SPY.csv`, create it once:
```bash
python -m venv .venv
source .venv/bin/activate
pip install -r scripts/requirements.txt
python scripts/run_verifier.py SPY --binary build/risk_sim --paths 200000 --notional 1e6
```
The script downloads SPY OHLCV data (Yahoo Finance), saves it to `data/SPY.csv`, and prints option/VaR comparisons.

## Heavy Testing
- **Stress harness**:
  ```bash
  ./build/risk_stress --jobs 8 --iterations 60 --paths 400000
  ```
  Reports mean/median/p99 latency, average OpenMP thread usage, option price dispersion, and VaR distribution.
- **CLI sweeps**:
  ```bash
  for p in 100000 200000 400000 800000; do
    ./build/risk_sim option --spot 100 --strike 100 --vol 0.2 --paths "$p" --format json
  done
  ```
- **Python verifier**: already shown above; useful for regression tests against analytic results.

## Repository Layout
```
include/                 # Public headers (MonteCarloEngine interface)
src/                     # C++ sources (risk_sim, risk_dashboard, risk_stress, engine)
frontend/                # React + Vite dashboard (src/ & dist/)
scripts/                 # Python verifier + requirements
build/                   # CMake build outputs (ignored in VC)
data/                    # Optional cached CSV + JSONL logs
CMakeLists.txt           # Root build configuration
README.md                # This file
```

## Future Enhancements
- Swap POSIX sockets for `epoll`/`io_uring` to push latency lower.
- Add WebSocket streaming for real-time simulation progress.
- Extend the engine to exotic derivatives (Asian, barrier) and calibrate against historical data.
- Package Docker compose for one-command deployment.
