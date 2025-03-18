import dayjs from "dayjs";
import { useDashboardStore } from "../store";

export function SummaryCards() {
  const simulations = useDashboardStore((state) => state.simulations);
  const option = simulations.find((run) => run.command === "option");
  const varRun = simulations.find((run) => run.command === "var");

  return (
    <div className="summary">
      <div className="card">
        <h3>Latest Option Price</h3>
        <p className="value">{option ? option.result.price.toFixed(4) : "—"}</p>
        <p className="meta">
          {option
            ? `± ${Number(option.result.standardError ?? 0).toFixed(5)} (threads: ${option.threadCount})`
            : "Run a simulation"}
        </p>
      </div>
      <div className="card">
        <h3>Latest Value-at-Risk</h3>
        <p className="value">{varRun ? varRun.result.valueAtRisk.toFixed(2) : "—"}</p>
        <p className="meta">
          {varRun ? `ES: ${Number(varRun.result.expectedShortfall ?? 0).toFixed(2)}` : "Run a simulation"}
        </p>
      </div>
      <div className="card">
        <h3>Most Recent Run</h3>
        <p className="value">
          {simulations.length > 0 ? dayjs(simulations[0].timestamp).format("YYYY-MM-DD HH:mm") : "—"}
        </p>
        <p className="meta">
          {simulations.length > 0
            ? `${simulations[0].command.toUpperCase()} · ${simulations[0].durationSeconds.toFixed(3)}s`
            : "Pending"}
        </p>
      </div>
    </div>
  );
}
