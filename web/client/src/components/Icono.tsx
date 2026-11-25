// Iconos de linea, dibujados a mano: cuatro o cinco no justifican una
// libreria. Heredan el color del texto (currentColor).
const TRAZOS: Record<string, string> = {
  // pulso de un monitor
  inicio: 'M3 11l9-7 9 7M5 9.5V20h5v-6h4v6h5V9.5',
  vivo: 'M3 12h4l2-6 4 12 2-6h6',
  // reloj con flecha de "volver"
  historial: 'M3 12a9 9 0 1 0 3-6.7M3 4v5h5M12 7v5l3 2',
  // perillas
  config: 'M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0M14 4v4M8 10v4M16 16v4',
  // dos personas
  usuarios: 'M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM2 21v-1a6 6 0 0 1 12 0v1M16 3.5a4 4 0 0 1 0 7M22 21v-1a6 6 0 0 0-4-5.6',
  sol: 'M12 4V2M12 22v-2M4 12H2M22 12h-2M5.6 5.6 4.2 4.2M19.8 19.8l-1.4-1.4M5.6 18.4l-1.4 1.4M19.8 4.2l-1.4 1.4M12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z',
  luna: 'M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z',
  // medio circulo lleno: "como el sistema"
  auto: 'M12 21a9 9 0 1 1 0-18 9 9 0 0 1 0 18zM12 3v18',
  // libreta con renglones
  bitacora: 'M6 3h11a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6zM6 3v18M10 8h5M10 12h5M10 16h3',
  // lineas de campo entre dos polos
  campo: 'M4 12c4-7 12-7 16 0M4 12c4 7 12 7 16 0M4 12h16',
  // menu del celular: tres renglones / cruz para cerrarlo
  menu: 'M4 6h16M4 12h16M4 18h16',
  cerrar: 'M6 6l12 12M18 6 6 18',
  // circulo con una "i"
  info: 'M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM12 11v5M12 8h.01',
  // una persona
  cuenta: 'M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM4 21a8 8 0 0 1 16 0',
  // puerta con flecha hacia afuera
  salir: 'M15 4h3a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-3M10 17l5-5-5-5M15 12H3',
  // birrete
  facultad: 'M2 9l10-5 10 5-10 5zM6 11v5c3 2.5 9 2.5 12 0v-5M22 9v6',
  // flecha hacia una bandeja
  descargar: 'M12 4v11M7 10l5 5 5-5M4 20h16',
  // caja con flecha saliendo
  externo: 'M14 4h6v6M20 4l-9 9M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5',
  // libro abierto
  manual: 'M12 6c-2-1.5-5-2-8-2v14c3 0 6 .5 8 2 2-1.5 5-2 8-2V4c-3 0-6 .5-8 2zM12 6v14',
  // chip / placa
  placa: 'M7 7h10v10H7zM10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4',
};

export function Icono({ nombre, tamaño = 16 }: { nombre: keyof typeof TRAZOS | string; tamaño?: number }) {
  return (
    <svg width={tamaño} height={tamaño} viewBox="0 0 24 24" fill="none" stroke="currentColor"
      strokeWidth={2} strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d={TRAZOS[nombre] ?? ''} />
    </svg>
  );
}
