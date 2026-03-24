import { styles as s } from "../styles/theme";

export function Controls({
  streaming,
  status,
  onStart,
  onStop,
  onOpenSettings,
}) {
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
      <button
        style={{
          ...s.btnBase,
          ...s.btnSettings,
          opacity: streaming ? 0.3 : 1,
          cursor: streaming ? "default" : "pointer",
        }}
        onClick={onOpenSettings}
        disabled={streaming}
      >
        <span style={s.btnIcon}>⚙</span> SETTINGS
      </button>
    </div>
  );
}
