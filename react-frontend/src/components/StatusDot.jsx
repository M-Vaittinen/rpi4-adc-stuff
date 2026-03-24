export function StatusDot({ status, streaming }) {
  return (
    <span
      style={{
        display: "inline-block",
        width: 8,
        height: 8,
        borderRadius: "50%",
        background:
          status === "connected"
            ? streaming
              ? "#39ff6e"
              : "#585b70"
            : "#e06c75",
        boxShadow:
          streaming && status === "connected" ? "0 0 8px #39ff6e" : "none",
        marginRight: 8,
      }}
    />
  );
}
