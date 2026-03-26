import { useState } from "react";
import { styles as s } from "../styles/theme";

export function Controls({
  streaming,
  status,
  onStart,
  onStop,
  sampleRate,
  setSampleRate,
  adcBits,
  setAdcBits,
  yAxisMode,
  setYAxisMode,
  maxVoltage,
  setMaxVoltage,
}) {
  const [recordTime, setRecordTime] = useState(1);

  const startDisabled = streaming || status !== "connected";
  const stopDisabled = !streaming;

  return (
    <div style={s.controls}>
      <button
        style={{
          ...s.btnBase,
          ...s.btnStart,
          opacity: startDisabled ? 0.3 : 1,
          cursor: startDisabled ? "default" : "pointer",
        }}
        onClick={onStart}
        disabled={startDisabled}
      >
        <span style={s.btnIcon}>▶</span> START
      </button>
      <button
        style={{
          ...s.btnBase,
          ...s.btnStop,
          opacity: stopDisabled ? 0.3 : 1,
          cursor: stopDisabled ? "default" : "pointer",
        }}
        onClick={onStop}
        disabled={stopDisabled}
      >
        <span style={s.btnIcon}>■</span> STOP
      </button>

      <div style={{ width: 1, height: 28, background: "#1a1a2e" }} />

      <input
        type="number"
        step="1"
        min="1"
        value={recordTime}
        onChange={(e) => setRecordTime(e.target.value)}
        style={{ ...s.input, width: 70 }}
      />
      <button
        style={{
          ...s.btnBase,
          ...s.btnStart,
          opacity: startDisabled ? 0.3 : 1,
          cursor: startDisabled ? "default" : "pointer",
        }}
        disabled={startDisabled}
        onClick={() => {
          onStart();
          setTimeout(() => onStop(), recordTime * 1000);
        }}
      >
        REC
      </button>

      <div
        style={{
          marginLeft: "auto",
          display: "flex",
          alignItems: "center",
          gap: 12,
        }}
      >
        <div style={{ display: "flex", flexDirection: "column", gap: 2 }}>
          <span style={s.fieldLabel}>Mode</span>
          <select
            value={yAxisMode}
            onChange={(e) => setYAxisMode(e.target.value)}
            style={s.input}
          >
            <option value="raw">Raw</option>
            <option value="voltage">Voltage</option>
          </select>
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 2 }}>
          <span style={s.fieldLabel}>Max Voltage (V)</span>
          <input
            type="number"
            step="0.1"
            min="0.1"
            value={maxVoltage}
            onChange={(e) => {
              const v = parseFloat(e.target.value);
              if (!isNaN(v) && v > 0) setMaxVoltage(v);
            }}
            disabled={yAxisMode !== "voltage"}
            style={{
              ...s.input,
              width: 90,
              opacity: yAxisMode !== "voltage" ? 0.3 : 1,
            }}
          />
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 2 }}>
          <span style={s.fieldLabel}>Sample Rate (Sa/s)</span>
          <input
            type="number"
            step="1000"
            min="1000"
            value={sampleRate}
            onChange={(e) => {
              const v = parseInt(e.target.value, 10);
              if (!isNaN(v) && v > 0) setSampleRate(v);
            }}
            style={{ ...s.input, width: 110 }}
          />
        </div>

        <div style={{ display: "flex", flexDirection: "column", gap: 2 }}>
          <span style={s.fieldLabel}>ADC Resolution</span>
          <select
            value={adcBits}
            onChange={(e) => setAdcBits(parseInt(e.target.value, 10))}
            style={s.input}
          >
            <option value={16}>16-bit</option>
            <option value={12}>12-bit</option>
          </select>
        </div>
      </div>
    </div>
  );
}
