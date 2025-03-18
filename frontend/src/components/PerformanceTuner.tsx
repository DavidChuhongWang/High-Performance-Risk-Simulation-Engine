import { FormEvent, useState } from "react";
import { useDashboardStore } from "../store";

export function PerformanceTuner() {
  const runOption = useDashboardStore((state) => state.runOption);
  const runVaR = useDashboardStore((state) => state.runVaR);
  const optionResult = useDashboardStore((state) => state.optionResult);
  const varResult = useDashboardStore((state) => state.varResult);
  const status = useDashboardStore((state) => state.status);

  const [optionPayload, setOptionPayload] = useState({
    spot: 100,
    strike: 100,
    rate: 0.02,
    dividend: 0.01,
    vol: 0.2,
    maturity: 1,
    steps: 252,
    paths: 300000,
    seed: 777,
    antithetic: true,
    control: true,
    block: 4096,
    type: "call" as "call" | "put"
  });

  const [varPayload, setVarPayload] = useState({
    spot: 100,
    rate: 0.02,
    vol: 0.2,
    maturity: 1,
    steps: 252,
    paths: 300000,
    seed: 1337,
    antithetic: true,
    control: false,
    block: 4096,
    percentile: 0.99,
    notional: 1_000_000
  });

  async function handleQuickOption(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    await runOption(optionPayload);
  }

  async function handleQuickVar(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    await runVaR(varPayload);
  }

  return (
    <div className="panel tuner-panel">
      <div className="panel-header">
        <h2>Performance Tuning & Quick Runs</h2>
        <span className={`status-indicator ${status}`}>{status.toUpperCase()}</span>
      </div>

      <div className="tuner-grid">
        <form className="tuner-card" onSubmit={handleQuickOption}>
          <h3>Option Pricing Sweep</h3>
          <div className="row">
            <label>
              Paths
              <input
                type="number"
                value={optionPayload.paths}
                step={50000}
                onChange={(event) => setOptionPayload((current) => ({ ...current, paths: Number(event.target.value) }))}
              />
            </label>
            <label>
              Vol
              <input
                type="number"
                step="0.01"
                value={optionPayload.vol}
                onChange={(event) => setOptionPayload((current) => ({ ...current, vol: Number(event.target.value) }))}
              />
            </label>
            <label>
              Maturity
              <input
                type="number"
                step="0.05"
                value={optionPayload.maturity}
                onChange={(event) =>
                  setOptionPayload((current) => ({ ...current, maturity: Number(event.target.value) }))
                }
              />
            </label>
          </div>
          <div className="row">
            <label>
              Antithetic
              <select
                value={optionPayload.antithetic ? "true" : "false"}
                onChange={(event) =>
                  setOptionPayload((current) => ({ ...current, antithetic: event.target.value === "true" }))
                }
              >
                <option value="true">true</option>
                <option value="false">false</option>
              </select>
            </label>
            <label>
              Control Variate
              <select
                value={optionPayload.control ? "true" : "false"}
                onChange={(event) =>
                  setOptionPayload((current) => ({ ...current, control: event.target.value === "true" }))
                }
              >
                <option value="true">true</option>
                <option value="false">false</option>
              </select>
            </label>
          </div>
          <button type="submit">Launch Option Run</button>
          {optionResult && (
            <pre className="tuner-output">{JSON.stringify(optionResult.result, null, 2)}</pre>
          )}
        </form>

        <form className="tuner-card" onSubmit={handleQuickVar}>
          <h3>VaR Scenario</h3>
          <div className="row">
            <label>
              Paths
              <input
                type="number"
                value={varPayload.paths}
                step={50000}
                onChange={(event) => setVarPayload((current) => ({ ...current, paths: Number(event.target.value) }))}
              />
            </label>
            <label>
              Vol
              <input
                type="number"
                step="0.01"
                value={varPayload.vol}
                onChange={(event) => setVarPayload((current) => ({ ...current, vol: Number(event.target.value) }))}
              />
            </label>
            <label>
              Percentile
              <input
                type="number"
                step="0.001"
                value={varPayload.percentile}
                min="0.8"
                max="0.999"
                onChange={(event) =>
                  setVarPayload((current) => ({ ...current, percentile: Number(event.target.value) }))
                }
              />
            </label>
          </div>
          <div className="row">
            <label>
              Notional
              <input
                type="number"
                step="100000"
                value={varPayload.notional}
                onChange={(event) => setVarPayload((current) => ({ ...current, notional: Number(event.target.value) }))}
              />
            </label>
            <label>
              Antithetic
              <select
                value={varPayload.antithetic ? "true" : "false"}
                onChange={(event) =>
                  setVarPayload((current) => ({ ...current, antithetic: event.target.value === "true" }))
                }
              >
                <option value="true">true</option>
                <option value="false">false</option>
              </select>
            </label>
          </div>
          <button type="submit">Launch VaR Run</button>
          {varResult && <pre className="tuner-output">{JSON.stringify(varResult.result, null, 2)}</pre>}
        </form>
      </div>
    </div>
  );
}
