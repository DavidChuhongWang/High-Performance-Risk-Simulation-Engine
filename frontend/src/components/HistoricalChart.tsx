import { useEffect } from "react";
import { CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";
import { useDashboardStore } from "../store";

export function HistoricalChart() {
  const { historical, refreshHistorical } = useDashboardStore((state) => ({
    historical: state.historical,
    refreshHistorical: state.refreshHistorical
  }));

  useEffect(() => {
    if (historical.length === 0) {
      void refreshHistorical();
    }
  }, [historical.length, refreshHistorical]);

  return (
    <div className="panel">
      <div className="panel-header">
        <h2>Historical Close Prices</h2>
        <button onClick={() => refreshHistorical()}>Refresh</button>
      </div>
      <ResponsiveContainer width="100%" height={320}>
        <LineChart data={historical} margin={{ top: 16, right: 24, left: 0, bottom: 0 }}>
          <CartesianGrid stroke="#1e293b" strokeDasharray="3 3" />
          <XAxis dataKey="date" stroke="#94a3b8" minTickGap={24} />
          <YAxis stroke="#94a3b8" domain={["auto", "auto"]} />
          <Tooltip
            contentStyle={{ background: "#1e293b", border: "1px solid #334155" }}
            labelStyle={{ color: "#e2e8f0" }}
          />
          <Line type="monotone" dataKey="close" stroke="#38bdf8" dot={false} strokeWidth={2} name="Close" />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
