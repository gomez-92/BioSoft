import { useEffect, useState } from 'react';

// Vuelve al comienzo de la pagina. Aparece recien cuando se bajo lo
// suficiente como para perder de vista la parte de arriba de la seccion: la
// pagina tiene un solo scroll (sin listas que scrolleen por dentro), y en las
// largas -- la configuracion, una corrida con todos sus graficos -- se vuelve
// a subir seguido.
const DESDE_PX = 400;

export function BotonArriba() {
  const [visible, setVisible] = useState(false);
  useEffect(() => {
    const revisar = () => setVisible(window.scrollY > DESDE_PX);
    revisar();
    window.addEventListener('scroll', revisar, { passive: true });
    return () => window.removeEventListener('scroll', revisar);
  }, []);
  if (!visible) return null;
  return (
    <button type="button" className="boton-arriba" title="Volver arriba" aria-label="Volver arriba"
      onClick={() => window.scrollTo({ top: 0, behavior: 'smooth' })}>
      ↑
    </button>
  );
}
