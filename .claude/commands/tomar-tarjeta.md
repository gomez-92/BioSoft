---
description: Toma una tarjeta (y sus hermanas) del board Trello "BioSoft", la mueve a "En curso" y evalua si la implementacion actual cumple los criterios de aprobacion
argument-hint: nombre
---

Cuando el usuario invoque `/tomar-tarjeta <nombre>`, ejecutar en este orden.
El board relevante es **"BioSoft"** (https://trello.com/b/lix4QCuG/biosoft), el
board de revision de codigo -- no confundir con "Plan PF - Fin" ni "Proyecto
Final", que son de features/montaje.

1. **Buscar la tarjeta**: usar `trelloSearch` o `trelloReadList`/`trelloReadBoard`
   sobre "BioSoft" para encontrar la tarjeta cuyo nombre matchea `<nombre>`
   (las tarjetas tienen prefijo `[Placa, Prioridad]`, ej. `[Mega2560, Critico]
   Detector / Source / Rule` -- buscar por coincidencia parcial, no exacta).
   Si hay mas de una coincidencia razonable, preguntar al usuario cual es.

2. **Detectar tarjetas hermanas**: revisar si la tarjeta tiene:
   - una companera de **"Detalle extendido"** (mismo ID + " — Detalle extendido",
     ver convencion en memoria `trello_boards_biosoft`) porque el contenido no
     entraba en el limite de 2048 caracteres,
   - o un link a una version anterior archivada / version viva mas nueva
     (convencion "Version anterior archivada en: ..." / "Correccion posterior
     en: ...").
   Si existen, incluirlas todas en los pasos siguientes (mover + evaluar juntas).

3. **Mover a "En curso"**: antes de mover, leer las listas reales del board
   (`trelloReadBoard`/`trelloReadList`) para confirmar el nombre exacto de la
   lista "En curso" -- puede diferir de lo que dice la memoria si el usuario la
   renombro. Mover la tarjeta principal y sus hermanas ahi con
   `trelloWriteCard action=move`.

4. **Leer el contenido completo**: `trelloReadCard` de la tarjeta (y su
   "Detalle extendido" si existe) para extraer las secciones **Que hace**,
   **Estado actual**, **Revision de codigo**, **Mejoras**, **Criterios de
   diseno**, **Criterios de aprobacion** y **Tests o evidencias**. Revisar
   tambien checklists (`trelloReadChecklist action=list_by_card`) si los hay.

5. **Evaluar contra el codigo actual**: leer el/los archivo(s) fuente
   relevantes en el repo (esp32/ o mega2560/ segun corresponda) y contrastar
   la implementacion real contra lo que dice la tarjeta -- no asumir que la
   tarjeta sigue vigente, el codigo puede haber cambiado desde que se escribio
   (ya paso con INT-001/003/004, ver memoria). Para cada criterio de
   "Criterios de aprobacion" listado en la tarjeta, indicar si el codigo
   actual lo cumple, con la referencia de archivo/linea que lo sustenta.

6. **Si no hay "Criterios de aprobacion" o "Tests o evidencias" en la
   tarjeta** (o estan vacios/desactualizados): proponer al usuario pruebas
   concretas para validar el funcionamiento, siguiendo la convencion ya usada
   en el board:
   - si el modulo es logica pura (sin hardware/LVGL/red), proponer un test
     unitario en host (`pio test -e native`, ver `mega2560/test/test_detector/`
     como referencia de estilo) -- verificar primero si ya existe cobertura
     real en `mega2560/test/` antes de asumir que falta,
   - si depende de hardware real (sensores, LVGL, red, coil), proponer una
     prueba manual en banco (que conectar, que observar en el serial
     monitor/pantalla, que valores esperar).
   No inventar que un test existe si no se pudo confirmar grepeando el
   codigo.

7. **Resumir al usuario**: en pocas lineas -- que tarjeta(s) se tomaron, a
   que lista se movieron, veredicto de cumplimiento de los criterios de
   aprobacion existentes (o su ausencia), y las pruebas propuestas si aplica.

No pedir confirmacion para mover la tarjeta -- invocar el comando ya es la
confirmacion explicita del usuario para ese paso.
