#!/bin/bash
#
# GPIO Toggle Test Tool
#
# Toggles a GPIO pin at a specified interval for testing GPIO capture.
# Default test pin: GPIO24 (header pin 18)
# Connect GPIO24 to GPIO25 (header pin 22) for trigger testing.
#
# Usage: gpio_toggle.sh [milliseconds]
#   milliseconds: delay between toggles (default: 100ms)
#
# Requires: libgpiod-utils package (gpioset command)
# Exit with Ctrl+C

TEST_GPIO=24
DELAY_MS=${1:-100}

# Validate delay
if ! [[ "$DELAY_MS" =~ ^[0-9]+$ ]] || [ "$DELAY_MS" -lt 1 ]; then
    echo "Invalid delay: $DELAY_MS (must be >= 1 ms)" >&2
    exit 1
fi

# Check if gpioset is available
if ! command -v gpioset &> /dev/null; then
    echo "Error: gpioset not found" >&2
    echo "Install with: sudo apt install libgpiod-utils" >&2
    exit 1
fi

FREQ=$(echo "scale=2; 1000 / ($DELAY_MS * 2)" | bc)

echo "GPIO Toggle Test Tool"
echo "====================="
echo "Test Pin: GPIO$TEST_GPIO (header pin 18)"
echo "Trigger:  GPIO25 (header pin 22) - connect these pins together"
echo "Interval: $DELAY_MS ms"
echo "Frequency: $FREQ Hz"
echo ""
echo "Toggling GPIO$TEST_GPIO. Press Ctrl+C to stop."
echo ""

# Use gpioset's --toggle option for clean toggling
# Pattern: high for DELAY_MS, low for DELAY_MS, repeat forever (0 = infinite loop)
gpioset --toggle ${DELAY_MS}ms,${DELAY_MS}ms,0 gpiochip0 ${TEST_GPIO}=1
