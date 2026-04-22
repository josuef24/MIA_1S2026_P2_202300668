import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// ── Configuración de Vite ────────────────────────────────────
export default defineConfig({
  plugins: [react()],
  server: {
    port: 3000,
    // Proxy local hacia el backend C++ en desarrollo
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/api/, '')
      }
    }
  },
  build: {
    outDir: 'dist'
  }
})
