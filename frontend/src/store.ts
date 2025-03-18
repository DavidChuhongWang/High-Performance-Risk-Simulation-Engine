import { create } from "zustand";
import type { SimulationRun, HistoricalPoint } from "./api";
import { fetchHistorical, fetchSimulations, submitOption, submitVaR } from "./api";

type StateStatus = "idle" | "loading" | "error";

interface DashboardState {
  simulations: SimulationRun[];
  historical: HistoricalPoint[];
  status: StateStatus;
  error?: string;
  selectedSymbol: string;
  statsWindow: number;
  optionResult?: any;
  varResult?: any;
  refreshSimulations: () => Promise<void>;
  refreshHistorical: (symbol?: string) => Promise<void>;
  runOption: (payload: Record<string, string | number | boolean>) => Promise<any>;
  runVaR: (payload: Record<string, string | number | boolean>) => Promise<any>;
  setStatsWindow: (value: number) => void;
}

export const useDashboardStore = create<DashboardState>((set, get) => ({
  simulations: [],
  historical: [],
  status: "idle",
  selectedSymbol: "SPY",
  statsWindow: 10,
  optionResult: undefined,
  varResult: undefined,
  async refreshSimulations() {
    set({ status: "loading", error: undefined });
    try {
      const data = await fetchSimulations();
      set({ simulations: data, status: "idle" });
    } catch (error: any) {
      set({ status: "error", error: error.message ?? "Failed to load simulations" });
    }
  },
  async refreshHistorical(symbol) {
    const target = symbol ?? get().selectedSymbol;
    set({ status: "loading", error: undefined });
    try {
      const data = await fetchHistorical();
      set({ historical: data, status: "idle", selectedSymbol: target });
    } catch (error: any) {
      set({ status: "error", error: error.message ?? "Failed to load historical data" });
    }
  },
  async runOption(payload) {
    set({ status: "loading", error: undefined });
    try {
      const result = await submitOption(payload);
      await get().refreshSimulations();
      set({ status: "idle", optionResult: result });
      return result;
    } catch (error: any) {
      set({ status: "error", error: error.message ?? "Option pricing failed" });
      throw error;
    }
  },
  async runVaR(payload) {
    set({ status: "loading", error: undefined });
    try {
      const result = await submitVaR(payload);
      await get().refreshSimulations();
      set({ status: "idle", varResult: result });
      return result;
    } catch (error: any) {
      set({ status: "error", error: error.message ?? "VaR simulation failed" });
      throw error;
    }
  },
  setStatsWindow(value: number) {
    set({ statsWindow: value });
  }
}));
