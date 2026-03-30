import { useRef, useCallback, useState } from "react";
import type { PlotData } from "./types";
import { useWebSocket } from "./hooks/useWebSocket";
import { WGLPlot } from "./components/WGLPlot";
import { generateSineData } from "./utils/sineData";
import { saveCanvasesAsImage } from "./utils/saveImage";
import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Slider } from "@/components/ui/slider";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  PlayIcon,
  StopIcon,
  FloppyDiskIcon,
  UploadSimpleIcon,
  DownloadSimpleIcon,
} from "@phosphor-icons/react";
import {
  INIT_CAP,
  DEFAULT_SAMPLE_RATE,
  DEFAULT_ADC_MAX,
  LIVE_WINDOW_SIZE,
  LIVE_WINDOW_MIN,
  LIVE_WINDOW_MAX,
  ADC_OPTIONS,
} from "@/config/constants";

// shadcn preset: --preset b4hIZmq00

function App() {
  // All sample data lives in a plain ref — no state, no re-renders on arrival.
  // Float32Array stores Uint16 values exactly (0–65535).
  const dataRef = useRef<PlotData>({
    ys: new Float32Array(INIT_CAP),
    count: 0,
  });

  const [live, setLive] = useState(false);
  const [windowSize, setWindowSize] = useState(LIVE_WINDOW_SIZE);
  const [sampleRate, setSampleRate] = useState(DEFAULT_SAMPLE_RATE);
  const [adcMax, setAdcMax] = useState<number>(DEFAULT_ADC_MAX);

  const handleData = useCallback((chunk: Uint16Array) => {
    const d = dataRef.current;
    const needed = d.count + chunk.length;

    if (needed > d.ys.length) {
      const newCap = Math.max(needed * 2, d.ys.length * 2);
      const grown = new Float32Array(newCap);
      grown.set(d.ys.subarray(0, d.count));
      d.ys = grown;
    }

    // Uint16 → Float32 conversion is implicit and lossless
    d.ys.set(chunk, d.count);
    d.count += chunk.length;
  }, []);

  const { status, streaming, sendCommand } = useWebSocket({
    onData: handleData,
  });

  function handleClear() {
    dataRef.current.count = 0;
  }

  return (
    <div className="flex h-screen flex-col p-4">
      <div className="mb-2 text-xs text-muted-foreground">
        status:{" "}
        <strong
          className={
            status === "connected" ? "text-emerald-400" : "text-orange-400"
          }
        >
          {status}
        </strong>
        {" | "}streaming: <strong>{String(streaming)}</strong>
      </div>

      <div className="mb-4 flex flex-wrap items-center gap-2">
        <Button
          onClick={() => {
            handleClear();
            dataRef.current = generateSineData(5_000_000);
          }}
        >
          Sine 5M
        </Button>
        <Button
          onClick={() => {
            dataRef.current = generateSineData(10_000_000);
          }}
        >
          Sine 10M
        </Button>
        <div className="ml-auto flex gap-2">
          <Button title="Import data" variant="outline">
            <UploadSimpleIcon data-icon="inline-start" />
            Import
          </Button>
          <Button title="Export data" variant="outline">
            <DownloadSimpleIcon data-icon="inline-start" />
            Export
          </Button>
          <Button
            title="Save current view as image"
            onClick={() => saveCanvasesAsImage("plotter")}
          >
            <FloppyDiskIcon data-icon="inline-start"></FloppyDiskIcon>
            save
          </Button>
        </div>
      </div>

      <WGLPlot
        id={"plotter"}
        dataRef={dataRef}
        style={{ flex: 1, minHeight: 0 }}
        sampleRate={sampleRate}
        adcMax={adcMax}
        live={live}
        windowSize={windowSize}
      />

      <div className="mt-2 flex flex-wrap items-center gap-2">
        <Button
          variant="green"
          title="Start streaming"
          disabled={streaming || status != "connected"}
          onClick={() => sendCommand("start")}
        >
          <PlayIcon data-icon="inline-start"></PlayIcon>
          Start
        </Button>
        <Button
          title="Stop streaming"
          disabled={!streaming}
          onClick={() => sendCommand("stop")}
        >
          <StopIcon data-icon="inline-start"></StopIcon>
          Stop
        </Button>
        <Button
          variant={"secondary"}
          disabled={status != "connected"}
          onClick={handleClear}
        >
          Clear
        </Button>
        <Label
          title="Lock view to the latest N samples and auto-scroll as new data arrives"
          className="gap-1.5"
        >
          <Checkbox
            checked={live}
            onCheckedChange={(checked) => setLive(checked === true)}
          />
          Live
        </Label>
        <Slider
          title="Live window size"
          value={[windowSize]}
          onValueChange={([v]) => setWindowSize(v)}
          min={LIVE_WINDOW_MIN}
          max={LIVE_WINDOW_MAX}
          step={1_000}
          disabled={!live}
          className="w-32"
        />
        <span className="w-14 text-xs text-muted-foreground">
          {(windowSize / 1000).toFixed(0)} k samples
        </span>
        <div className="ml-auto flex items-center gap-2">
          <div
            title="Select ADC bit resolution"
            className="flex items-center gap-1.5 text-xs"
          >
            ADC
            <Select
              value={String(adcMax)}
              onValueChange={(v) => setAdcMax(Number(v))}
            >
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {ADC_OPTIONS.map((opt) => (
                  <SelectItem key={opt.value} value={String(opt.value)}>
                    {opt.label}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>
          <Label className="gap-1.5">
            sample rate (Hz)
            <Input
              type="number"
              min={1}
              value={sampleRate}
              onChange={(e) => setSampleRate(Number(e.target.value) || 1)}
              className="w-24"
            />
          </Label>
        </div>
      </div>
    </div>
  );
}

export default App;
