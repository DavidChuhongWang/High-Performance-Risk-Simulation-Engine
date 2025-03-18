import { FormEvent, useState } from "react";
import { useDashboardStore } from "../store";

interface OptionResultPayload {
  market: {
    spot: number;
    rate: number;
    dividend: number;
    vol: number;
  };
  simulation: {
    maturity: number;
    steps: number;
    paths: number;
    seed: number;
    antithetic: boolean;
    control: boolean;
    block: number;
  };
  strike: number;
  option_type: "call" | "put";
}

export function SimulationForms() {
  const runOption = useDashboardStore((state) => state.runOption);
  const runVaR = useDashboardStore((state) => state.runVaR);

  const [optionForm, setOptionForm] = useState<OptionResultPayload>({
    market: { spot: 100, rate: 0.02, dividend: 0.01, vol: 0.2 },
    simulation: { maturity: 1, steps: 252, paths: 200_000, seed: 42, antithetic: true, control: true, block: 4096 },
    strike: 100,
    option_type: "call"
  });

  const [varForm, setVarForm] = useState({
    spot: 100,
    rate: 0.02,
    vol: 0.2,
    maturity: 1,
    steps: 252,
    paths: 200_000,
    seed: 1337,
    antithetic: true,
    control: false,
    block: 4096,
    percentile: 0.99,
    notional: 1_000_000
  });

  async function handleOptionSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const payload = {
      spot: optionForm.market.spot,
      rate: optionForm.market.rate,
      dividend: optionForm.market.dividend,
      vol: optionForm.market.vol,
      maturity: optionForm.simulation.maturity,
      steps: optionForm.simulation.steps,
      paths: optionForm.simulation.paths,
      seed: optionForm.simulation.seed,
      antithetic: optionForm.simulation.antithetic,
      control: optionForm.simulation.control,
      block: optionForm.simulation.block,
      strike: optionForm.strike,
      type: optionForm.option_type
    };
    await runOption(payload);
  }

  async function handleVaRSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const payload = {
      spot: varForm.spot,
      rate: varForm.rate,
      vol: varForm.vol,
      maturity: varForm.maturity,
      steps: varForm.steps,
      paths: varForm.paths,
      seed: varForm.seed,
      antithetic: varForm.antithetic,
      control: varForm.control,
      block: varForm.block,
      percentile: varForm.percentile,
      notional: varForm.notional
    };
    await runVaR(payload);
  }

  return (
    <div className="forms">
      <form className="panel" onSubmit={handleOptionSubmit}>
        <h2>European Option Pricing</h2>
        <div className="row">
          <label>
            Spot
            <input
              type="number"
              step="0.01"
              value={optionForm.market.spot}
              onChange={(event) =>
                setOptionForm((current) => ({
                  ...current,
                  market: { ...current.market, spot: Number(event.target.value) }
                }))
              }
              required
            />
          </label>
          <label>
            Strike
            <input
              type="number"
              step="0.5"
              value={optionForm.strike}
              onChange={(event) =>
                setOptionForm((current) => ({ ...current, strike: Number(event.target.value) }))
              }
              required
            />
          </label>
          <label>
            Volatility
            <input
              type="number"
              step="0.01"
              value={optionForm.market.vol}
              onChange={(event) =>
                setOptionForm((current) => ({
                  ...current,
                  market: { ...current.market, vol: Number(event.target.value) }
                }))
              }
              required
            />
          </label>
        </div>
        <div className="row">
          <label>
            Maturity (years)
            <input
              type="number"
              step="0.05"
              value={optionForm.simulation.maturity}
              onChange={(event) =>
                setOptionForm((current) => ({
                  ...current,
                  simulation: { ...current.simulation, maturity: Number(event.target.value) }
                }))
              }
              required
            />
          </label>
          <label>
            Paths
            <input
              type="number"
              step="10000"
              value={optionForm.simulation.paths}
              onChange={(event) =>
                setOptionForm((current) => ({
                  ...current,
                  simulation: { ...current.simulation, paths: Number(event.target.value) }
                }))
              }
              required
            />
          </label>
          <label>
            Option Type
            <select
              value={optionForm.option_type}
              onChange={(event) =>
                setOptionForm((current) => ({ ...current, option_type: event.target.value as "call" | "put" }))
              }
            >
              <option value="call">Call</option>
              <option value="put">Put</option>
            </select>
          </label>
        </div>
        <button type="submit">Run Option Simulation</button>
      </form>

      <form className="panel" onSubmit={handleVaRSubmit}>
        <h2>Portfolio VaR</h2>
        <div className="row">
          <label>
            Spot
            <input
              type="number"
              step="0.01"
              value={varForm.spot}
              onChange={(event) => setVarForm((current) => ({ ...current, spot: Number(event.target.value) }))}
              required
            />
          </label>
          <label>
            Volatility
            <input
              type="number"
              step="0.01"
              value={varForm.vol}
              onChange={(event) => setVarForm((current) => ({ ...current, vol: Number(event.target.value) }))}
              required
            />
          </label>
          <label>
            Paths
            <input
              type="number"
              step="10000"
              value={varForm.paths}
              onChange={(event) => setVarForm((current) => ({ ...current, paths: Number(event.target.value) }))}
              required
            />
          </label>
        </div>
        <div className="row">
          <label>
            Percentile
            <input
              type="number"
              step="0.001"
              value={varForm.percentile}
              onChange={(event) => setVarForm((current) => ({ ...current, percentile: Number(event.target.value) }))}
              required
            />
          </label>
          <label>
            Notional
            <input
              type="number"
              step="50000"
              value={varForm.notional}
              onChange={(event) => setVarForm((current) => ({ ...current, notional: Number(event.target.value) }))}
              required
            />
          </label>
        </div>
        <button type="submit">Run VaR Simulation</button>
      </form>
    </div>
  );
}
