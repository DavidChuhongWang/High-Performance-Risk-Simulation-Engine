import { useMemo } from "react";
import { AreaChart, Area, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid, BarChart, Bar } from "recharts";
import { useDashboardStore } from "../store";

export function PerformanceCharts() {
  const simulations = useDashboardStore((state) => state.simulations);
  const statsWindow = useDashboardStore((state) => state.statsWindow);
  const setStatsWindow = useDashboardStore((state) => state.setStatsWindow);

  const recent = useMemo(() => simulations.slice(0, statsWindow).reverse(), [simulations, statsWindow]);

  const throughputSeries = useMemo(
    () =>
      recent
        .filter((run) => run.throughputPerSec && run.command === "option")
        .map((run) => ({
          timestamp: run.timestamp,
          throughput: Math.round(run.throughputPerSec ?? 0),
          paths: run.samplesProcessed ?? 0
        })),
    [recent]
  );

  const durationSeries = useMemo(
    () =>
      recent.map((run) => ({
        timestamp: run.timestamp,
        duration: run.durationSeconds,
        command: run.command.toUpperCase()
      })),
    [recent]
  );

  const pctOption = simulations.filter((run) => run.command === "option").length;
  const pctVar = simulations.filter((run) => run.command === "var").length;
  const total = Math.max(1, simulations.length);

  return (
    <div className="panel performance-panel">
      <div className="panel-header">
        <h2>Workload Performance</h2>
        <label className="select-inline">
          <span>Window</span>
          <select value={statsWindow} onChange={(event) => setStatsWindow(Number(event.target.value))}>
            {[5, 10, 20, 40, 80].map((n) => (
              <option key={n} value={n}>
                {n}
              </option>
            ))}
          </select>
        </label>
      </div>

      <div className="charts-grid">
        <div className="chart-card">
          <h3>Throughput (paths/sec)</h3>
          <ResponsiveContainer width="100%" height={240}>
            <AreaChart data={throughputSeries} margin={{ top: 16, right: 16, left: 0, bottom: 0 }}>
              <CartesianGrid stroke="#1e293b" strokeDasharray="3 3" />
              <XAxis dataKey="timestamp" stroke="#94a3b8" minTickGap={24} />
              <YAxis stroke="#94a3b8" />
              <Tooltip
                contentStyle={{ background: "#1e293b", border: "1px solid #334155" }}
                labelStyle={{ color: "#e2e8f0" }}
              />
              <Area type="monotone" dataKey="throughput" stroke="#38bdf8" fill="rgba(56,189,248,0.35)" />
            </AreaChart>
          </ResponsiveContainer>
        </div>

        <div className="chart-card">
          <h3>Run Duration (seconds)</h3>
          <ResponsiveContainer width="100%" height={240}>
            <BarChart data={durationSeries} margin={{ top: 16, right: 16, left: 0, bottom: 0 }}>
              <CartesianGrid stroke="#1e293b" strokeDasharray="3 3" />
              <XAxis dataKey="timestamp" stroke="#94a3b8" minTickGap={24} />
              <YAxis stroke="#94a3b8" />
              <Tooltip
                contentStyle={{ background: "#1e293b", border: "1px solid #334155" }}
                labelStyle={{ color: "#e2e8f0" }}
              />
              <Bar dataKey="duration" fill="#a855f7" />
            </BarChart>
          </ResponsiveContainer>
        </div>
      </div>

      <div className="chart-card distribution-card">
        <h3>Workload Mix</h3>
        <div className="distribution-bar">
          <div className="option" style={{ width: `${(pctOption / total) * 100}%` }} />
          <div className="var" style={{ width: `${(pctVar / total) * 100}%` }} />
        </div>
        <ul>
          <li>Option runs: {pctOption}</li>
          <li>VaR runs   : {pctVar}</li>
          <li>Total jobs : {total}</li>
        </ul>
      </div>
    </div>
  );
}
