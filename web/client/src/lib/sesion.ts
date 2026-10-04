import { createContext, useContext } from 'react';
import type { Perfil } from './types.js';

// Quien esta usando el monitor. Las pantallas lo leen para decidir que
// botones mostrar (borrar, enviar configuracion); el servidor lo vuelve a
// verificar en cada pedido, asi que esconder un boton es comodidad, no
// seguridad.
export const SesionContext = createContext<Perfil | null>(null);
export function useSesion(): Perfil | null { return useContext(SesionContext); }
export function useEsAdmin(): boolean { return useContext(SesionContext)?.role === 'admin'; }
