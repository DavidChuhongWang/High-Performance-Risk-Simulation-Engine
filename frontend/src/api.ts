import axios from "axios";

export interface SimulationRun {
  command: string;
  timestamp: string;
  durationSeconds: number;
  threadCount: number;
  samplesProcessed?: number;
  throughputPerSec?: number;
  result: Record<string, any>;
  input?: Record<string, any>;
}

export interface HistoricalPoint {
  symbol: string;
  date: string;
  open: number;
  high: number;
  low: number;
  close: number;
  adjustedClose: number;
  volume: number;
}

const client = axios.create({
  baseURL: "/api"
});

export async function fetchSimulations(): Promise<SimulationRun[]> {
  const { data } = await client.get<SimulationRun[]>("/simulations");
  return data;
}

export async function fetchHistorical(limit = 120): Promise<HistoricalPoint[]> {
  const { data } = await client.get<HistoricalPoint[]>("/historical", { params: { limit } });
  return data;
}

export async function submitOption(params: Record<string, string | number | boolean>) {
  const { data } = await client.get("/option", { params });
  return data;
}

export async function submitVaR(params: Record<string, string | number | boolean>) {
  const { data } = await client.get("/var", { params });
  return data;
}
