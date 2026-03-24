import { useEffect } from "react";
import { usePlotly } from "../hooks/usePlotly";
import { styles as s } from "../styles/theme";

export function ChannelPlot({ channel, yAxisMode, refVoltage, adcBits, sampleRate, onSlotReady }) {
  const { plotRef, indexRef, pendingX, pendingY, rafRef, sampleCount, flushToPlot, resetPlot } =
    usePlotly({ channel, yAxisMode, refVoltage, adcBits, sampleRate });

  // Register this channel's buffer refs with the parent
  useEffect(() => {
    onSlotReady(channel.id, { indexRef, pendingX, pendingY, rafRef, flushToPlot, resetPlot });
  }, [channel.id, onSlotReady, indexRef, pendingX, pendingY, rafRef, flushToPlot, resetPlot]);

  return (
    <div style={s.channelWrap}>
      <div style={s.channelHeader}>
        <span style={{ ...s.channelName, color: channel.color }}>{channel.name}</span>
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
