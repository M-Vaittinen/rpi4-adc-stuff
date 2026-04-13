#!/usr/bin/env python3
"""
example_read.py — Read ADC chunks from the mvaring ring buffer in shared memory.

Prerequisites:
    1. Build and install the extension (from the py_mvaring/ directory):
           pip install --user -e .
       or build in-place:
           python setup.py build_ext --inplace

    2. The rpi_adc_stream process must be running (as root) on the RPi so that
       the shared memory '/RPI_ADC_BUFF' exists and is being populated.

Usage:
    python example_read.py [--chunks N]
"""

import array
import argparse
import sys
import time

import mvaring

# POSIX shared memory name used by rpi_adc_stream (defined as SHM_NAME in adc_common.h)
SHM_NAME = "/RPI_ADC_BUFF"


def adc_raw_val(word: int) -> int:
    """Extract the 11-bit ADC value from a raw 32-bit SPI word.

    Mirrors the C macro:  ADC_RAW_VAL(d) = (((uint16_t)(d)<<8 | (uint16_t)(d)>>8) & 0x7ff)
    """
    d16 = word & 0xFFFF
    return (((d16 << 8) | (d16 >> 8)) & 0xFFFF) & 0x7FF


def decode_samples(raw_bytes: bytes) -> array.array:
    """Convert the raw 'samples' bytes field to an array of uint32 values."""
    vals = array.array("I")  # unsigned int (32-bit)
    vals.frombytes(raw_bytes)
    return vals


def main():
    parser = argparse.ArgumentParser(description="Read from mvaring ring buffer")
    parser.add_argument("--chunks", type=int, default=16,
                        help="Maximum chunks to read per call (default: 16)")
    parser.add_argument("--loop", action="store_true",
                        help="Keep reading until interrupted (Ctrl-C)")
    args = parser.parse_args()

    print(f"Opening shared memory '{SHM_NAME}' ...")
    try:
        handle = mvaring.open(SHM_NAME)
    except OSError as e:
        print(f"Error: {e}", file=sys.stderr)
        print("Is rpi_adc_stream running?  (sudo ./rpi_adc_stream)", file=sys.stderr)
        sys.exit(1)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Ring buffer OK : {mvaring.is_ok(handle)}")
    print(f"Buffer version : {mvaring.MVARING_VERSION}")
    print(f"Chunks capacity: {mvaring.NUM_DATA_CHUNKS}")
    print(f"Samples/chunk  : {mvaring.MAX_SAMPS}")
    print()

    total_chunks = 0
    try:
        while True:
            avail = mvaring.available(handle)
            if avail == 0:
                if not args.loop:
                    print("No data available in ring buffer.")
                    break
                time.sleep(0.01)
                continue

            chunks = mvaring.read(handle, args.chunks)
            total_chunks += len(chunks)

            for i, chunk in enumerate(chunks):
                usecs = chunk["usecs"]
                samples = decode_samples(chunk["samples"])
                gpio    = decode_samples(chunk["gpio_lev0"])

                # Extract 11-bit ADC values from the raw SPI words
                adc_values = [adc_raw_val(s) for s in samples]

                print(f"  chunk[{total_chunks - len(chunks) + i:5d}]  "
                      f"usecs={usecs:10d}  "
                      f"adc[0:4]={adc_values[:4]}  "
                      f"gpio[0]={gpio[0]:#010x}")

            if not args.loop:
                break

    except KeyboardInterrupt:
        print("\nInterrupted.")

    print(f"\nTotal chunks read: {total_chunks}")
    mvaring.close(handle)
    print("Handle closed.")


if __name__ == "__main__":
    main()
