# ADC Simulator for Testing UI

## Purpose

`adc-simulate` generates simulated ADC data for testing the trigger visualization
without requiring actual Raspberry Pi hardware or ADC chip.

## Building

```bash
cd test
make adc-simulate
```

## Running

Terminal 1 - Start simulator:
```bash
./adc-simulate
```

Terminal 2 - Start UI (in parent directory):
```bash
./test-ui
```

## Simulated Data

- **Waveform**: 1 kHz sine wave
- **Amplitude**: 0 to 3.3 Volts
- **Sample Rate**: 1 MSPS (1 million samples/second)
- **GPIO**: All pins default to 0, can be controlled interactively

## Commands

While the simulator is running, you can use these commands:

### `trigger <gpio> <value>`
Set a GPIO pin to 0 or 1. This simulates a trigger signal change.

Example - Create rising edge on GPIO25:
```
sim> trigger 25 1
```

Example - Create falling edge on GPIO25:
```
sim> trigger 25 0
```

### `show`
Display current GPIO state (all 32 pins)

### `stats`
Show ring buffer statistics (available chunks, dropped count, etc.)

### `help`
Display command help

### `quit` or `exit`
Stop the simulator

## Testing Trigger Visualization

1. Start the simulator: `./adc-simulate`
2. In another terminal, start the UI: `./test-ui`
3. You should see a sine wave scrolling
4. In the simulator, create a trigger:
   ```
   sim> trigger 25 1
   ```
5. The UI should pause with a vertical arrow showing:
   - Where the trigger occurred
   - Direction (up arrow for rising edge)

6. Press spacebar in the UI to resume
7. Create a falling edge trigger:
   ```
   sim> trigger 25 0
   ```
8. UI should pause again with down arrow

## Notes

- The simulator runs at approximately 1 MSPS to match real hardware
- Data is written continuously to the shared memory ring buffer
- Multiple GPIO pins can be set simultaneously
- GPIO state is included with each sample in the ring buffer
- Ctrl+C or 'quit' command stops the simulator cleanly
