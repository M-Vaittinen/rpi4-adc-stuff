import { useState, useRef, useCallback } from "react";
import { useWebSocket } from "../hooks/useWebSocket";
import { Header } from "./Header";
import { Controls } from "./Controls";
import { ChannelPlot } from "./ChannelPlot";
import { styles as s } from "../styles/theme";
import {
  PLOT0,
  DEFAULT_SAMPLE_RATE,
  DEFAULT_ADC_BITS,
  DEFAULT_MAX_VOLTAGE,
  DEFAULT_Y_AXIS_MODE,
} from "../config/constants";

export default function ADCPlotter() {
  const [yAxisMode, setYAxisMode] = useState(DEFAULT_Y_AXIS_MODE);
  const [maxVoltage, setMaxVoltage] = useState(DEFAULT_MAX_VOLTAGE);
  const [adcBits, setAdcBits] = useState(DEFAULT_ADC_BITS);
  const [sampleRate, setSampleRate] = useState(DEFAULT_SAMPLE_RATE);

  const slotRef = useRef(null);
  const [slotReady, setSlotReady] = useState(false);

  const handleSlotReady = useCallback((slot) => {
    slotRef.current = slot;
    setSlotReady(true);
  }, []);

  const { status, streaming, sendCommand } = useWebSocket({
    adcBits,
    sampleRate,
    slot: slotReady ? slotRef.current : null,
  });

  return (
    <div style={s.root}>
      <Header status={status} streaming={streaming} />

      <div style={s.plotContainer}>
        <ChannelPlot
          channel={PLOT0}
          adcBits={adcBits}
          sampleRate={sampleRate}
          streaming={streaming}
          onSlotReady={handleSlotReady}
        />
      </div>

      <Controls
        streaming={streaming}
        status={status}
        onStart={() => sendCommand("start")}
        onStop={() => sendCommand("stop")}
        sampleRate={sampleRate}
        setSampleRate={setSampleRate}
        adcBits={adcBits}
        setAdcBits={setAdcBits}
        yAxisMode={yAxisMode}
        setYAxisMode={setYAxisMode}
        maxVoltage={maxVoltage}
        setMaxVoltage={setMaxVoltage}
      />
    </div>
  );
}
