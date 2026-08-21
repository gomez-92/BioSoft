---
description: Cierra la rama de feature actual -- commit, merge a main, push, borra la rama local y documenta el trabajo
---

Cuando el usuario invoque `/cerrar_rama`, ejecutar en este orden. La rama
trunk de este repo es `main` (no `master` -- el repo no tiene una rama con
ese nombre, aunque el usuario a veces diga "master" para referirse a ella).

1. **Revisar estado**: `git status` y `git diff` de la rama actual. Si hay
   archivos sospechosos de contener secretos/credenciales entre los
   cambios, avisar y frenar en vez de commitear a ciegas.

2. **Documentar**: actualizar `CLAUDE.md` con lo que se trabajo en esta
   rama -- bugs corregidos, decisiones de arquitectura, flujos nuevos --
   siguiendo el estilo ya usado ahi (bullets dentro de la seccion `esp32`
   o `mega2560` que corresponda). Documentar solo lo que no es obvio o
   derivable leyendo el codigo; no repetir el diff en prosa.

3. **Commit**: si hay cambios sin commitear (incluida la actualizacion de
   CLAUDE.md), commitear con mensaje en espanol, estilo imperativo sin
   tildes, enfocado en el "por que" mas que en el "que" (ver `git log`
   reciente para el tono exacto). Si no hay nada pendiente, saltar este
   paso.

4. **Merge a main**: `git checkout main`, `git pull` si hace falta, y
   `git merge <rama-que-se-cierra>` (fast-forward cuando sea posible).

5. **Push**: `git push origin main`.

6. **Borrar la rama local** que se acaba de cerrar (`git branch -d
   <rama>`). No borrar `main` ni ninguna otra rama.

Este comando ya es la confirmacion explicita del usuario para todo el
flujo (commit + merge + push + borrar rama) -- no volver a preguntar en
cada paso. Al terminar, resumir en pocas lineas: que se commiteo, el
merge/push, y que se documento en CLAUDE.md.
