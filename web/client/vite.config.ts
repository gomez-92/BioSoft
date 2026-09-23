import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// El front pega a /api y /socket.io contra el backend. Con el proxy en
// desarrollo no hay CORS ni URLs absolutas en el codigo, asi que el mismo
// build sirve en produccion detras de un solo dominio.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://localhost:4000',
      '/socket.io': { target: 'http://localhost:4000', ws: true },
    },
  },
});
