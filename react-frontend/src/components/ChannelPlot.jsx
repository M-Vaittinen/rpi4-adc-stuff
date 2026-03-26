import { useEffect } from "react";
import { usePlotly } from "../hooks/usePlotly";
import { styles as s } from "../styles/theme";

export function ChannelPlot({
  channel,
  adcBits,
  sampleRate,
  streaming,
  onSlotReady,
}) {
  const {
    plotRef,
    indexRef,
    pendingX,
    pendingY,
    rafRef,
    sampleCount,
    flushToPlot,
    resetPlot,
  } = usePlotly({
    channel,
    adcBits,
    sampleRate,
    streaming,
  });

  useEffect(() => {
    onSlotReady({ indexRef, pendingX, pendingY, rafRef, flushToPlot, resetPlot });
  }, [onSlotReady, indexRef, pendingX, pendingY, rafRef, flushToPlot, resetPlot]);

  return (
    <div style={s.channelWrap}>
      <div style={s.channelHeader}>
        <span style={{ ...s.channelName, color: channel.color }}>
          {channel.name}
        </span>
        <span style={s.channelSamples}>
          <span style={s.statLabel}>SAMPLES </span>
          <span style={s.statValue}>{sampleCount.toLocaleString()}</span>
        </span>
      </div>
      <div style={s.channelPlot}>
        <div ref={plotRef} style={s.plot} />
      </div>
    </div>
  );
}
