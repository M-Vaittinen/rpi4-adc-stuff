import { StatusDot } from "./StatusDot";
import { styles as s } from "../styles/theme";

export function Header({ status, streaming }) {
  return (
    <div style={s.header}>
      <div style={s.headerLeft}>
        <StatusDot status={status} streaming={streaming} />
        <span style={s.logo}>ADC</span>
        <span style={s.logoSub}>SCOPE</span>
        <span
          style={{
            ...s.statusText,
            color:
              status === "connected"
                ? "#39ff6e"
                : status === "disconnected"
                  ? "#e06c75"
                  : "#585b70",
          }}
        >
          {status}
        </span>
      </div>
      <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
        <button style={{ ...s.btnBase, ...s.btnNeutral }}>Import</button>
        <button style={{ ...s.btnBase, ...s.btnNeutral }}>Export</button>
      </div>
    </div>
  );
}
