import { useState, useRef, useCallback } from "react";
import { useWebSocket } from "../hooks/useWebSocket";
import { Header } from "./Header";
import { Controls } from "./Controls";
import { SettingsModal } from "./SettingsModal";
import { ChannelPlot } from "./ChannelPlot";
import { styles as s } from "../styles/theme";
import {
  CHANNELS,
  DEFAULT_SAMPLE_RATE,
  DEFAULT_ADC_BITS,
  DEFAULT_REF_VOLTAGE,
  DEFAULT_Y_AXIS_MODE,
} from "../config/constants";

export default function ADCPlotter() {
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [yAxisMode, setYAxisMode] = useState(DEFAULT_Y_AXIS_MODE);
  const [adcBits, setAdcBits] = useState(DEFAULT_ADC_BITS);
  const [refVoltage, setRefVoltage] = useState(DEFAULT_REF_VOLTAGE);
  const [sampleRate, setSampleRate] = useState(DEFAULT_SAMPLE_RATE);

  // Registry of per-channel buffer slots, keyed by channel id.
  // Filled by ChannelPlot components via onSlotReady callback.
  const slotsRef = useRef({});
  const [slotsReady, setSlotsReady] = useState(false);

  const handleSlotReady = useCallback((channelId, slot) => {
    slotsRef.current[channelId] = slot;
    if (Object.keys(slotsRef.current).length === CHANNELS.length) {
      setSlotsReady(true);
    }
  }, []);

  // Build ordered array of slots for the WebSocket hook
  const channelSlots = slotsReady
    ? CHANNELS.map((ch) => slotsRef.current[ch.id])
    : [];

  const { status, streaming, sendCommand } = useWebSocket({
    yAxisMode,
    adcBits,
    refVoltage,
    sampleRate,
    channelSlots,
  });

  return (
    <div style={s.root}>
      <Header status={status} streaming={streaming} />

      <div style={s.plotContainer}>
        {CHANNELS.map((ch) => (
          <ChannelPlot
            key={ch.id}
            channel={ch}
            yAxisMode={yAxisMode}
            refVoltage={refVoltage}
            adcBits={adcBits}
            sampleRate={sampleRate}
            onSlotReady={handleSlotReady}
          />
        ))}
      </div>

      <Controls
        streaming={streaming}
        status={status}
        onStart={() => sendCommand("start")}
        onStop={() => sendCommand("stop")}
        onOpenSettings={() => setSettingsOpen(true)}
      />

      {settingsOpen && (
        <SettingsModal
          yAxisMode={yAxisMode}
          setYAxisMode={setYAxisMode}
          adcBits={adcBits}
          setAdcBits={setAdcBits}
          sampleRate={sampleRate}
          setSampleRate={setSampleRate}
          refVoltage={refVoltage}
          setRefVoltage={setRefVoltage}
          onClose={() => setSettingsOpen(false)}
        />
      )}
    </div>
  );
}
