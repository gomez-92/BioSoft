import React from 'react';
import ReactDOM from 'react-dom/client';
import { App } from './App.js';
import { aplicarPreferencia, leerPreferencia } from './lib/tema.js';
import './styles.css';

// El tema antes del primer dibujo: si no, la pagina aparece un instante con
// el tema por defecto y despues cambia.
aplicarPreferencia(leerPreferencia());

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
