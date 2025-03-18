import { useEffect } from "react";
import { HistoricalChart } from "./components/HistoricalChart";
import { SimulationForms } from "./components/SimulationForms";
import { SummaryCards } from "./components/SummaryCards";
import { PerformanceCharts } from "./components/PerformanceCharts";
import { PerformanceTuner } from "./components/PerformanceTuner";
import { useDashboardStore } from "./store";

import "./app.css";

export default function App() {
  const { refreshSimulations, status, error } = useDashboardStore((state) => ({
    refreshSimulations: state.refreshSimulations,
    status: state.status,
    error: state.error
  }));

  useEffect(() => {
    void refreshSimulations();
  }, [refreshSimulations]);

  return (
    <div className="container">
      <header>
        <div>
          <h1>Risk Simulation Dashboard</h1>
          <p>High-performance Monte Carlo analytics served from a C++ backend.</p>
        </div>
        <div className={`status ${status}`}>
          {status === "loading" && <span>Loading…</span>}
          {status === "error" && <span>Error: {error}</span>}
          {status === "idle" && <span>Ready</span>}
        </div>
      </header>

      <SummaryCards />
      <SimulationForms />
      <PerformanceTuner />
      <PerformanceCharts />
      <HistoricalChart />
    </div>
  );
}
