import { styles as s } from "../styles/theme";

export function SettingsModal({
  yAxisMode,
  setYAxisMode,
  adcBits,
  setAdcBits,
  sampleRate,
  setSampleRate,
  refVoltage,
  setRefVoltage,
  onClose,
}) {
  return (
    <div style={s.modalOverlay} onClick={onClose}>
      <div style={s.modal} onClick={(e) => e.stopPropagation()}>
        <div style={s.modalHeader}>
          <span style={s.modalTitle}>Settings</span>
          <button style={s.modalClose} onClick={onClose}>
            ✕
          </button>
        </div>

        <div style={s.modalBody}>
          <label style={s.fieldLabel}>Y-Axis Mode</label>
          <div style={s.toggleRow}>
            <button
              style={{
                ...s.toggleBtn,
                ...(yAxisMode === "voltage" ? s.toggleActive : {}),
              }}
              onClick={() => setYAxisMode("voltage")}
            >
              Voltage
            </button>
            <button
              style={{
                ...s.toggleBtn,
                ...(yAxisMode === "raw" ? s.toggleActive : {}),
              }}
              onClick={() => setYAxisMode("raw")}
            >
              Raw
            </button>
          </div>

          <label style={s.fieldLabel}>ADC Resolution</label>
          <div style={s.toggleRow}>
            <button
              style={{
                ...s.toggleBtn,
                ...(adcBits === 16 ? s.toggleActive : {}),
              }}
              onClick={() => setAdcBits(16)}
            >
              16-bit
            </button>
            <button
              style={{
                ...s.toggleBtn,
                ...(adcBits === 12 ? s.toggleActive : {}),
              }}
              onClick={() => setAdcBits(12)}
            >
              12-bit
            </button>
          </div>

          <label style={s.fieldLabel}>Sample Rate (Sa/s)</label>
          <input
            id="sample-rate-input"
            type="number"
            step="100"
            min="1"
            value={sampleRate}
            onChange={(e) => {
              const v = parseInt(e.target.value, 10);
              if (!isNaN(v) && v > 0) setSampleRate(v);
            }}
            style={s.input}
          />

          <label style={s.fieldLabel}>Reference Voltage (V)</label>
          <input
            id="ref-voltage-input"
            type="number"
            step="0.1"
            min="0.1"
            value={refVoltage}
            onChange={(e) => {
              const v = parseFloat(e.target.value);
              if (!isNaN(v) && v > 0) setRefVoltage(v);
            }}
            style={s.input}
          />
        </div>
      </div>
    </div>
  );
}
