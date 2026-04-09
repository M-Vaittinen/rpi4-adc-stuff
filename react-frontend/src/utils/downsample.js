/**
 * Largest-Triangle-Three-Buckets (LTTB) downsampling.
 *
 * Reduces a time series of `x.length` points to `threshold` points while
 * preserving visual shape (peaks, valleys, edges). O(n) single-pass.
 *
 * Reference: Sveinn Steinarsson, "Downsampling Time Series for Visual
 * Representation", University of Iceland, 2013.
 */
export function lttbDownsample(x, y, threshold) {
  const len = x.length;
  if (threshold >= len || threshold < 3) {
    return { x: x.slice(), y: y.slice() };
  }

  const outX = new Array(threshold);
  const outY = new Array(threshold);

  // First point always included
  outX[0] = x[0];
  outY[0] = y[0];

  const every = (len - 2) / (threshold - 2);
  let aIndex = 0;

  for (let i = 0; i < threshold - 2; i++) {
    // Current bucket boundaries
    const rangeStart = Math.floor(i * every) + 1;
    const rangeEnd = Math.min(Math.floor((i + 1) * every) + 1, len);

    // Next bucket average (used for triangle area calculation)
    const nextStart = Math.floor((i + 1) * every) + 1;
    const nextEnd = Math.min(Math.floor((i + 2) * every) + 1, len);

    let avgX = 0;
    let avgY = 0;
    for (let j = nextStart; j < nextEnd; j++) {
      avgX += x[j];
      avgY += y[j];
    }
    const nextLen = nextEnd - nextStart;
    avgX /= nextLen;
    avgY /= nextLen;

    // Point A (previously selected)
    const ax = x[aIndex];
    const ay = y[aIndex];

    // Pick the point in the current bucket that forms the largest triangle
    let maxArea = -1;
    let bestIdx = rangeStart;

    for (let j = rangeStart; j < rangeEnd; j++) {
      const area = Math.abs(
        (ax - avgX) * (y[j] - ay) - (ax - x[j]) * (avgY - ay),
      );
      if (area > maxArea) {
        maxArea = area;
        bestIdx = j;
      }
    }

    outX[i + 1] = x[bestIdx];
    outY[i + 1] = y[bestIdx];
    aIndex = bestIdx;
  }

  // Last point always included
  outX[threshold - 1] = x[len - 1];
  outY[threshold - 1] = y[len - 1];

  return { x: outX, y: outY };
}
