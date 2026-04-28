import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "./ui/select";
import { Button } from "./ui/button";
import { Separator } from "./ui/separator";
import { Input } from "./ui/input";
import { Label } from "./ui/label";
import { Checkbox } from "./ui/checkbox";
import { Slider } from "./ui/slider";
import { PlayIcon, StopIcon } from "@phosphor-icons/react";
import {
  LIVE_WINDOW_MIN,
  LIVE_WINDOW_MAX,
  LIVE_WINDOW_STEP_SIZE,
} from "@/config/constants";

interface PlotterControlsProps {
  durationMs: number | null;
  setDurationMs: (v: number | null) => void;
  streaming: boolean;
  status: string;
  sendCommand: (cmd: any) => void;
  sampleRate: number;
  handleClear: () => void;
  live: boolean;
  setLive: (v: boolean) => void;
  fitAll: boolean;
  setFitAll: (v: boolean) => void;
  windowSize: number;
  setWindowSize: (v: number) => void;
}

export function PlotterControls({
  durationMs,
  setDurationMs,
  streaming,
  status,
  sendCommand,
  sampleRate,
  handleClear,
  live,
  setLive,
  fitAll,
  setFitAll,
  windowSize,
  setWindowSize,
}: PlotterControlsProps) {
  return (
    <>
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
          <SelectContent position="popper" className="max-h-60 overflow-y-auto">
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
          }}
        >
          <PlayIcon data-icon="inline-start" />
        </Button>
        <Button
          title="Stop streaming"
          disabled={!streaming}
          onClick={() => {
            sendCommand({ command: "stop" });
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
    </>
  );
}
