#!/usr/bin/env python3
"""Download historical equity data, run the C++ Monte Carlo engine, and verify outputs."""
from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path
from typing import Dict, Tuple

import numpy as np
import pandas as pd
import yfinance as yf


def black_scholes_price(spot: float, strike: float, rate: float, dividend: float, vol: float, maturity: float,
                        is_call: bool = True) -> float:
    if maturity <= 0 or vol <= 0:
        intrinsic = max(0.0, spot - strike) if is_call else max(0.0, strike - spot)
        return intrinsic
    sqrt_t = math.sqrt(maturity)
    sigma_sqrt_t = vol * sqrt_t
    d1 = (math.log(spot / strike) + (rate - dividend + 0.5 * vol * vol) * maturity) / sigma_sqrt_t
    d2 = d1 - sigma_sqrt_t
    from math import erf, sqrt

    def norm_cdf(x: float) -> float:
        return 0.5 * (1.0 + erf(x / math.sqrt(2.0)))

    disc_rate = math.exp(-rate * maturity)
    disc_div = math.exp(-dividend * maturity)
    if is_call:
        return spot * disc_div * norm_cdf(d1) - strike * disc_rate * norm_cdf(d2)
    return strike * disc_rate * norm_cdf(-d2) - spot * disc_div * norm_cdf(-d1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify risk_sim outputs against historical data")
    parser.add_argument("symbol", type=str, nargs="?", default="SPY", help="Ticker symbol (default: SPY)")
    parser.add_argument("--start", type=str, default=None, help="Start date YYYY-MM-DD (default: two years ago)")
    parser.add_argument("--end", type=str, default=None, help="End date YYYY-MM-DD (default: today)")
    parser.add_argument("--strike", type=float, default=None, help="Strike for option test (default: ATM)")
    parser.add_argument("--maturity", type=float, default=1.0, help="Option maturity in years")
    parser.add_argument("--rate", type=float, default=0.02, help="Continuous risk-free rate")
    parser.add_argument("--dividend", type=float, default=0.01, help="Continuous dividend yield")
    parser.add_argument("--paths", type=int, default=400_000, help="Monte Carlo paths per run")
    parser.add_argument("--notional", type=float, default=1_000_000.0, help="Notional for VaR comparison")
    parser.add_argument("--percentile", type=float, default=0.99, help="VaR percentile")
    parser.add_argument("--binary", type=Path, default=Path("build/risk_sim"), help="Path to risk_sim binary")
    parser.add_argument("--stress-runs", type=int, default=3, help="Extra convergence runs with increasing paths")
    return parser.parse_args()


def download_history(symbol: str, start: str | None, end: str | None) -> pd.DataFrame:
    data = yf.download(symbol, start=start, end=end, progress=False, auto_adjust=False)
    if data.empty:
        raise SystemExit(f"No historical data returned for {symbol}. Adjust dates or check the ticker.")
    data = data.rename(columns={
        "Open": "open",
        "High": "high",
        "Low": "low",
        "Close": "close",
        "Adj Close": "adj_close",
        "Volume": "volume",
    })
    data = data.dropna()
    data.index = pd.to_datetime(data.index)
    return data


def realised_statistics(history: pd.DataFrame) -> Tuple[float, float]:
    close = history["close"]
    returns = np.log(close / close.shift(1)).dropna()
    mu = returns.mean() * 252.0
    sigma = returns.std(ddof=1) * math.sqrt(252.0)
    return float(mu), float(sigma)


def run_risk_sim(binary: Path, command: str, args: Dict[str, float | int | bool | str]) -> Dict:
    if not binary.exists():
        raise SystemExit(f"risk_sim binary not found at {binary}. Build the project first.")
    cli = [str(binary), command]
    for key, value in args.items():
        cli.append(f"--{key}={value}")
    cli.append("--format=json")
    result = subprocess.run(cli, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"risk_sim failed ({command}): {result.stderr or result.stdout}")
    return json.loads(result.stdout)


def compare_var(empirical_losses: np.ndarray, percentile: float, simulated: float) -> Tuple[float, float]:
    if empirical_losses.size == 0:
        return float("nan"), float("nan")
    empirical_var = float(np.quantile(empirical_losses, percentile, method="higher"))
    diff = simulated - empirical_var
    rel = diff / empirical_var if empirical_var != 0 else float("inf")
    return empirical_var, rel


def main() -> None:
    args = parse_args()
    history = download_history(args.symbol, args.start, args.end)
    spot = float(history["close"].iloc[-1])
    strike = args.strike if args.strike is not None else spot
    mu, sigma = realised_statistics(history)

    print(f"Symbol           : {args.symbol}")
    print(f"Data points      : {history.shape[0]}")
    print(f"Latest close     : {spot:.4f}")
    print(f"Realised drift   : {mu:.6f}")
    print(f"Realised vol     : {sigma:.6f}")

    option_payload = {
        "spot": spot,
        "strike": strike,
        "rate": args.rate,
        "dividend": args.dividend,
        "vol": sigma,
        "maturity": args.maturity,
        "steps": 252,
        "paths": args.paths,
        "seed": 42,
        "antithetic": "true",
        "control": "true",
        "block": 4096,
        "type": "call"
    }
    option_response = run_risk_sim(args.binary, "option", option_payload)
    mc_price = float(option_response["result"]["price"])
    mc_std_err = float(option_response["result"].get("standardError", 0.0))
    analytic = black_scholes_price(spot, strike, args.rate, args.dividend, sigma, args.maturity, is_call=True)
    diff = mc_price - analytic
    rel_err = diff / analytic if analytic != 0 else float("inf")

    print("\n=== Option Pricing Check ===")
    print(f"Monte Carlo price  : {mc_price:.6f} ± {mc_std_err:.6f}")
    print(f"Black-Scholes price: {analytic:.6f}")
    print(f"Diff / Relative    : {diff:.6f} ({rel_err * 100:.4f}%)")

    returns = np.log(history["close"].values[1:] / history["close"].values[:-1])
    pnl = -args.notional * returns  # Loss is negative return * notional
    var_payload = {
        "spot": spot,
        "rate": args.rate,
        "vol": sigma,
        "maturity": 1.0,
        "steps": 252,
        "paths": args.paths,
        "seed": 1337,
        "antithetic": "true",
        "control": "false",
        "block": 4096,
        "percentile": args.percentile,
        "notional": args.notional
    }
    var_response = run_risk_sim(args.binary, "var", var_payload)
    simulated_var = float(var_response["result"]["valueAtRisk"])
    empirical_var, rel_gap = compare_var(pnl, args.percentile, simulated_var)

    print("\n=== Value-at-Risk Check ===")
    print(f"Simulated VaR  : {simulated_var:,.2f}")
    print(f"Empirical VaR  : {empirical_var:,.2f}")
    if not math.isnan(rel_gap):
        print(f"Relative gap   : {rel_gap * 100:.3f}%")

    if args.stress_runs > 0:
        print("\n=== Convergence Sweep ===")
        for i in range(args.stress_runs):
            paths = args.paths * (i + 1)
            sweep_payload = option_payload.copy()
            sweep_payload["paths"] = paths
            sweep_payload["seed"] = 100 + i
            response = run_risk_sim(args.binary, "option", sweep_payload)
            price = float(response["result"]["price"])
            stderr = float(response["result"].get("standardError", 0.0))
            print(f"paths={paths:>9}  price={price:.6f}  stderr={stderr:.6f}")

    print("\nVerification complete. Adjust parameters to explore other scenarios.")


if __name__ == "__main__":
    main()
