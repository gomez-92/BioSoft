// Iconos de linea, dibujados a mano: cuatro o cinco no justifican una
// libreria. Heredan el color del texto (currentColor).
const TRAZOS: Record<string, string> = {
  // pulso de un monitor
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
};

export function Icono({ nombre, tamaño = 16 }: { nombre: keyof typeof TRAZOS | string; tamaño?: number }) {
  return (
    <svg width={tamaño} height={tamaño} viewBox="0 0 24 24" fill="none" stroke="currentColor"
      strokeWidth={2} strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d={TRAZOS[nombre] ?? ''} />
    </svg>
  );
}
