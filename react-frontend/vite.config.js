import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  optimizeDeps: {
    include: ["plotly.js-gl2d-dist-min"],
  },
  build: {
    // Plotly is big — suppress the warning since we can't shrink it further
    chunkSizeWarningLimit: 2000,
  },
});
