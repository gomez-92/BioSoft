# Imagen del monitor: backend + front compilado, un solo proceso y un solo
# puerto. El front sale del mismo origen que la API (ver server/produccion.ts).

FROM node:22-alpine AS build
WORKDIR /app

# Las dependencias se copian primero para que Docker reuse esa capa mientras
# solo cambie el codigo.
COPY package.json package-lock.json ./
COPY server/package.json server/
COPY client/package.json client/
RUN npm ci --no-audit --no-fund

COPY . .
RUN npm run build

# Y recien despues se reduce node_modules a lo que hace falta para EJECUTAR:
# sin typescript, vite ni vitest, y sin las dependencias del cliente, que ya
# esta compilado y no se ejecuta en node.
#
# Esto va en la MISMA etapa a proposito. Instalar en las dos etapas hace que
# BuildKit corra los dos `npm ci` en paralelo, y los dos juntos se quedan sin
# memoria dentro del contenedor: npm falla con "Exit handler never called!",
# que no dice absolutamente nada sobre la causa real.
RUN rm -rf node_modules \
 && npm ci --omit=dev --workspace server --include-workspace-root --no-audit --no-fund

# --- imagen final ---
FROM node:22-alpine
WORKDIR /app
ENV NODE_ENV=production

COPY --from=build /app/package.json /app/package-lock.json ./
COPY --from=build /app/server/package.json server/
COPY --from=build /app/node_modules ./node_modules
COPY --from=build /app/server/dist ./server/dist
COPY --from=build /app/client/dist ./client/dist

# No correr como root.
USER node

# El proceso arranca desde server/ porque produccion.ts resuelve el front como
# ../client/dist relativo al directorio de trabajo.
WORKDIR /app/server
EXPOSE 4000

# El chequeo pega al unico endpoint publico.
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s \
  CMD node -e "fetch('http://localhost:'+(process.env.PORT||4000)+'/api/ping').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

CMD ["node", "dist/index.js"]
