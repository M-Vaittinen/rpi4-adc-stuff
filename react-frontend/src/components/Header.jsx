import { StatusDot } from "./StatusDot";
import { styles as s } from "../styles/theme";

export function Header({ status, streaming }) {
  return (
    <div style={s.header}>
      <div style={s.headerLeft}>
        <StatusDot status={status} streaming={streaming} />
        <span style={s.logo}>ADC</span>
        <span style={s.logoSub}>SCOPE</span>
        <span style={s.statusText}>{status}</span>
      </div>
    </div>
  );
}
