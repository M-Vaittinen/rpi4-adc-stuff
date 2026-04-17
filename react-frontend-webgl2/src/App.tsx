import { useRef, useCallback, useState, type WheelEvent } from "react";
import type { PlotData } from "./types";
import { useWebSocket, type ParsedFrame } from "./hooks/useWebSocket";
import { WGLPlot } from "./components/WGLPlot";
import { StatusDot } from "./components/StatusDot";
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
import { Separator } from "@/components/ui/separator";
import {
  PlayIcon,
  StopIcon,
  FloppyDiskIcon,
  UploadSimpleIcon,
  DownloadSimpleIcon,
} from "@phosphor-icons/react";
import {
  INIT_CAP,
  MAX_SAMPS,
  DEFAULT_SAMPLE_RATE,
  DEFAULT_ADC_MAX,
  LIVE_WINDOW_SIZE,
  LIVE_WINDOW_MIN,
  LIVE_WINDOW_MAX,
  LIVE_WINDOW_STEP_SIZE,
  ADC_OPTIONS,
  NOT_IMPLEMENTED,
  APP_VERSION,
} from "@/config/constants";

// shadcn preset: --preset b4hIZmq00

function App() {
  // All sample data lives in a plain ref — no state, no re-renders on arrival.
  // Float32Array stores Uint16 values exactly (0–65535).
  const dataRef = useRef<PlotData>({
    ys: new Float32Array(INIT_CAP),
    count: 0,
    chunkUsecs: new Float64Array(Math.ceil(INIT_CAP / MAX_SAMPS)),
    chunkCount: 0,
  });

  const [live, setLive] = useState(false);
  const [fitAll, setFitAll] = useState(false);
  const [windowSize, setWindowSize] = useState(LIVE_WINDOW_SIZE);
  const [sampleRate, setSampleRate] = useState(DEFAULT_SAMPLE_RATE);
  const [adcMax, setAdcMax] = useState<number>(DEFAULT_ADC_MAX);

  const [elapsedMs, _setElapsedMs] = useState<number | null>(null);
  // const timerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  // const streamStartRef = useRef<number | null>(null);

  // function startTimer() {
  //   streamStartRef.current = performance.now();
  //   setElapsedMs(0);
  //   timerRef.current = setInterval(() => {
  //     setElapsedMs(performance.now() - streamStartRef.current!);
  //   }, 100);
  // }

  // function stopTimer() {
  //   if (timerRef.current !== null) clearInterval(timerRef.current);
  //   if (streamStartRef.current !== null)
  //     setElapsedMs(performance.now() - streamStartRef.current);
  // }

  const handleData = useCallback((frame: ParsedFrame) => {
    const d = dataRef.current;

    // Detect stream restart: if the new first timestamp is more than 1 s behind
    // the last stored timestamp, rpi_adc_stream restarted and its hardware clock
    // reset.  Clear the buffer so the x-axis stays coherent.
    if (d.chunkCount > 0 && frame.chunkUsecs.length > 0) {
      const lastTs = d.chunkUsecs[d.chunkCount - 1];
      const newTs = frame.chunkUsecs[0];
      if (newTs < lastTs - 1_000_000) {
        d.count = 0;
        d.chunkCount = 0;
      }
    }

    const chunk = frame.samples;
    const needed = d.count + chunk.length;

    if (needed > d.ys.length) {
      const newCap = Math.max(needed * 2, d.ys.length * 2);
      const grown = new Float32Array(newCap);
      grown.set(d.ys.subarray(0, d.count));
      d.ys = grown;
    }

    d.ys.set(chunk, d.count);
    d.count += chunk.length;

    // Append chunk timestamps
    const newChunkCount = d.chunkCount + frame.chunkUsecs.length;
    if (newChunkCount > d.chunkUsecs.length) {
      const newCap = Math.max(newChunkCount * 2, d.chunkUsecs.length * 2);
      const grown = new Float64Array(newCap);
      grown.set(d.chunkUsecs.subarray(0, d.chunkCount));
      d.chunkUsecs = grown;
    }
    for (let i = 0; i < frame.chunkUsecs.length; i++) {
      d.chunkUsecs[d.chunkCount + i] = frame.chunkUsecs[i];
    }
    d.chunkCount = newChunkCount;
  }, []);

  const { status, streaming, sendCommand } = useWebSocket({
    onData: handleData,
    adcMax,
  });

  const handleWheel = useCallback(
    (e: WheelEvent<HTMLDivElement>) => {
      if (!live) return;

      setWindowSize((prev) => {
        const delta =
          e.deltaY > 0 ? LIVE_WINDOW_STEP_SIZE : -LIVE_WINDOW_STEP_SIZE;
        return Math.min(
          LIVE_WINDOW_MAX,
          Math.max(LIVE_WINDOW_MIN, prev + delta),
        );
      });
    },
    [live],
  );

  function handleClear() {
    dataRef.current.count = 0;
    dataRef.current.ys = new Float32Array(INIT_CAP);
    dataRef.current.chunkCount = 0;
    dataRef.current.chunkUsecs = new Float64Array(
      Math.ceil(INIT_CAP / MAX_SAMPS),
    );
  }

  return (
    <div className="flex h-screen flex-col p-4">
      <div className="flex h-5 items-center gap-2 mb-4 text-xs text-muted-foreground">
        <span>ADC Plotter v{APP_VERSION}</span>{" "}
        <Separator orientation="vertical" />
        <StatusDot status={status} streaming={streaming} />
        status:
        <strong
          className={
            status === "connected" ? "text-emerald-400" : "text-orange-400"
          }
        >
          {status}
        </strong>
        <Separator orientation="vertical" />
        elapsed:{" "}
        <strong>
          {elapsedMs === null ? "—" : (elapsedMs / 1000).toFixed(1) + " s"}
        </strong>
        <Button
          onClick={() => {
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
          <span title="Not yet implemented">
            <Button variant="outline" disabled>
              <DownloadSimpleIcon data-icon="inline-start" />
              Import
            </Button>
          </span>
          <span title="Not yet implemented">
            <Button variant="outline" disabled>
              <UploadSimpleIcon data-icon="inline-start" />
              Export
            </Button>
          </span>
          <Button
            title="Save current view as image"
            onClick={() => saveCanvasesAsImage("plotter")}
          >
            <FloppyDiskIcon data-icon="inline-start"></FloppyDiskIcon>
            Save
          </Button>
        </div>
      </div>

      <WGLPlot
        onWheel={handleWheel}
        id={"plotter"}
        dataRef={dataRef}
        style={{ flex: 1, minHeight: 0 }}
        sampleRate={sampleRate}
        adcMax={adcMax}
        live={live}
        fitAll={fitAll}
        windowSize={windowSize}
      />

      <div className="mt-2 flex flex-wrap items-center gap-3">
        <div className="flex items-center gap-2">
          <Select defaultValue="0">
            <SelectTrigger
              className="w-32"
              data-slot="input-group-control"
              title="Select amount of time for plotting"
            >
              <SelectValue placeholder="Duration" />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="0">Continuous</SelectItem>
              <SelectItem disabled={NOT_IMPLEMENTED} value="1">
                1 second (example)
              </SelectItem>
              <SelectItem disabled={NOT_IMPLEMENTED} value="2">
                2 seconds (example)
              </SelectItem>
            </SelectContent>
          </Select>
          <Button
            variant="green"
            title="Start streaming"
            disabled={streaming || status != "connected"}
            onClick={() => {
              sendCommand("start");
              // startTimer();
            }}
          >
            <PlayIcon data-icon="inline-start" />
          </Button>
          <Button
            title="Stop streaming"
            disabled={!streaming}
            onClick={() => {
              sendCommand("stop");
              // stopTimer();
            }}
          >
            <StopIcon data-icon="inline-start" />
          </Button>
          <Separator orientation="vertical" className="h-6" />
          <Button variant="secondary" onClick={handleClear}>
            Clear
          </Button>
        </div>

        <Separator orientation="vertical" className="h-6" />

        <div className="flex items-center gap-2">
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
          <Label
            title="Always show all recorded data while streaming (zoom/pan disabled)"
            className="gap-1.5"
          >
            <Checkbox
              checked={fitAll}
              disabled={!live}
              onCheckedChange={(checked) => setFitAll(checked === true)}
            />
            Fit all
          </Label>
          <Slider
            title="Live window size"
            value={[windowSize]}
            onValueChange={([v]) => setWindowSize(v)}
            min={LIVE_WINDOW_MIN}
            max={LIVE_WINDOW_MAX}
            step={LIVE_WINDOW_STEP_SIZE}
            disabled={!live || fitAll}
            className="w-32"
          />
          <Input
            id="window-size-input"
            type="number"
            min={LIVE_WINDOW_MIN}
            max={LIVE_WINDOW_MAX}
            step={LIVE_WINDOW_STEP_SIZE}
            value={[windowSize].toString()}
            disabled={!live || fitAll}
            onChange={(e) =>
              setWindowSize(Number(e.target.value) || LIVE_WINDOW_MIN)
            }
            className="w-24"
          />
          <span className="text-xs text-muted-foreground">samples</span>
        </div>

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
          <Separator orientation="vertical" className="h-6" />
          <Label className="gap-1.5" title="Static sample rate">
            sample rate (S/s)
            <Input
              id="samplerate"
              type="number"
              min={1}
              value={sampleRate}
              onChange={(e) => setSampleRate(Number(e.target.value) || 1)}
              className="w-24"
              disabled={NOT_IMPLEMENTED}
            />
          </Label>
        </div>
      </div>
    </div>
  );
}

export default App;
