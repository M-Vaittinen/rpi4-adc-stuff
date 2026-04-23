import { useRef, useCallback, useState, type WheelEvent } from "react";
import type { PlotData } from "./types";
import { useWebSocket, type ParsedFrame } from "./hooks/useWebSocket";
import { WGLPlot } from "./components/WGLPlot";
import { StatusDot } from "./components/StatusDot";
// import { generateSineData } from "./utils/sineData";
import { saveCanvasesAsImage } from "./utils/saveImage";
import { saveAsCsv } from "./utils/saveCsv";
import { loadCsvFile } from "./utils/loadCsv";
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
import { Toaster } from "@/components/ui/sonner";
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
  APP_VERSION,
} from "@/config/constants";
import { toast } from "sonner";

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

  const importInputRef = useRef<HTMLInputElement>(null);

  const [live, setLive] = useState(false);
  const [fitAll, setFitAll] = useState(false);
  const [windowSize, setWindowSize] = useState(LIVE_WINDOW_SIZE);
  const [sampleRate, setSampleRate] = useState(DEFAULT_SAMPLE_RATE);
  const [actualSampleRate, setActualSampleRate] = useState<number | null>(null);
  const [adcMax, setAdcMax] = useState<number>(DEFAULT_ADC_MAX);
  // null = continuous, otherwise duration in ms
  const [durationMs, setDurationMs] = useState<number | null>(null);

  // const [elapsedMs, setElapsedMs] = useState<number | null>(null);
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

    // Detect stream restart: rpi_adc_stream resets its hardware clock on each
    // start, so new timestamps begin at 0. Any new timestamp that is less than
    // the last stored timestamp means a new session began — clear the buffer so
    // the x-axis stays coherent.
    if (d.chunkCount > 0 && frame.chunkUsecs.length > 0) {
      const lastTs = d.chunkUsecs[d.chunkCount - 1];
      const newTs = frame.chunkUsecs[0];
      if (newTs < lastTs) {
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
    onInfo: (msg) => {
      if (msg.type === "actual_sample_rate") {
        console.log("Actual sample-rate:", msg.value, "Hz");
        setActualSampleRate(msg.value);
      }
    },
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
        {/* <Separator orientation="vertical" />
        elapsed:
        <strong>
          {elapsedMs === null ? "—" : (elapsedMs / 1000).toFixed(2) + " s"}
        </strong> */}
        {/* <Button
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
        </Button> */}
        <div className="ml-auto flex gap-2">
          <input
            ref={importInputRef}
            type="file"
            accept=".csv"
            className="hidden"
            onChange={(e) => {
              const file = e.target.files?.[0];
              if (!file) return;
              loadCsvFile(
                file,
                (data) => {
                  dataRef.current = data;
                  toast.success("Data imported successfully", {
                    position: "top-center",
                  });
                },
                (e) =>
                  toast.error(`Import failed: ${e}`, {
                    position: "top-center",
                  }),
              );
              e.target.value = "";
            }}
          />
          <Button
            variant="outline"
            title="Import CSV file"
            onClick={() => importInputRef.current?.click()}
          >
            <DownloadSimpleIcon data-icon="inline-start" />
            Import
          </Button>
          <Button
            variant="outline"
            title="Export all samples to CSV"
            onClick={() =>
              saveAsCsv(dataRef.current, (e) =>
                toast.error(`Export failed: ${e}`, {
                  position: "top-center",
                }),
              )
            }
          >
            <UploadSimpleIcon data-icon="inline-start" />
            Export
          </Button>
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
        actualSampleRate={actualSampleRate}
        adcMax={adcMax}
        live={live}
        fitAll={fitAll}
        windowSize={windowSize}
      />

      <div className="mt-2 flex flex-wrap items-center gap-3">
        <div className="flex items-center gap-2">
          <Select
            value={durationMs === null ? "0" : String(durationMs)}
            onValueChange={(v) => setDurationMs(v === "0" ? null : Number(v))}
          >
            <SelectTrigger
              className="w-32"
              data-slot="input-group-control"
              title="Select amount of time for plotting"
            >
              <SelectValue placeholder="Duration" />
            </SelectTrigger>
            <SelectContent
              position="popper"
              className="max-h-60 overflow-y-auto"
            >
              <SelectItem value="0">Continuous</SelectItem>
              {Array.from({ length: 20 }, (_, i) => (i + 1) * 5).map(
                (seconds) => (
                  <SelectItem key={seconds} value={String(seconds * 1000 + 50)}>
                    {seconds} seconds
                  </SelectItem>
                ),
              )}
            </SelectContent>
          </Select>
          <Button
            variant="green"
            title="Start streaming"
            disabled={streaming || status != "connected"}
            onClick={() => {
              sendCommand({
                command: "start",
                sampleRate,
                ...(durationMs !== null && { durationMs }),
              });
              // startTimer();
            }}
          >
            <PlayIcon data-icon="inline-start" />
          </Button>
          <Button
            title="Stop streaming"
            disabled={!streaming}
            onClick={() => {
              sendCommand({ command: "stop" });
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
          <Label className="gap-1.5" title="Sample rate">
            sample rate (S/s)
            <Input
              id="samplerate"
              type="number"
              min={1000}
              step={1000}
              value={sampleRate}
              onChange={(e) => setSampleRate(Number(e.target.value) || 1000)}
              onBlur={(e) => {
                const rounded =
                  Math.round(Number(e.target.value) / 1000) * 1000;
                setSampleRate(Math.max(1000, rounded));
              }}
              className="w-24"
              disabled={streaming}
            />
          </Label>
        </div>
      </div>
      <Toaster />
    </div>
  );
}

export default App;
