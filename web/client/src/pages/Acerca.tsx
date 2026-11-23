import { Icono } from '../components/Icono.js';

// "Acerca de": el proyecto, su autor y la facultad. Todo el texto esta en
// ACERCA, aca arriba, para editarlo sin tocar el marcado. Un campo vacio
// ('' o lista vacia) no se muestra: lo que todavia no esta escrito no aparece
// como hueco ni como "completar".
//
// Es publica (decision del autor, 2026-11-23): se abre tambien desde el
// login, sin sesion. Todo lo que este aca lo ve cualquiera que tenga la
// direccion del monitor, correo incluido.

const LACEMAB = 'Laboratorio de Compatibilidad Electromagnética y Bioingeniería (LACEMAB)';

const ACERCA = {
  proyecto: {
    sigla: 'SEMBAF',
    nombre: 'Sistema de Exposición Magnética de Baja Frecuencia',
    resumen:
      'SEMBAF es un sistema que expone animales de laboratorio a un campo ' +
      'electromagnético de baja frecuencia, controlado y medido, para un experimento de ' +
      'investigación en bioingeniería. Mantiene la intensidad pedida, vigila el campo y la ' +
      'temperatura durante toda la exposición, y corta el experimento por sí solo cuando se ' +
      'cumple el tiempo, cuando detecta peligro o cuando las condiciones dejan de ser ' +
      'científicamente válidas.',
    grupos:
      'Cada corrida es del grupo tratado (campo aplicado) o del grupo control (campo nulo: las ' +
      'bobinas se excitan igual, con la mitad en contrafase, para que el campo neto se anule). Las ' +
      'dos condiciones se eligen antes de empezar y no se pueden cambiar con el experimento en curso.',
    desarrollo: 'Software desarrollado con asistencia de Claude Code (Anthropic).',
  },
  piezas: [
    {
      icono: 'placa',
      titulo: 'Placa de control',
      texto: 'Arduino Mega 2560. Genera la señal de las bobinas, regula la intensidad en lazo ' +
        'cerrado, mide campo, corriente y temperatura, y decide cuándo cortar. La seguridad vive acá.',
    },
    {
      icono: 'config',
      titulo: 'Pantalla del equipo',
      texto: 'ESP32 con pantalla táctil. El operador configura y lanza cada corrida en el ' +
        'laboratorio, y la placa publica la telemetría por MQTT.',
    },
    {
      icono: 'vivo',
      titulo: 'Monitor remoto',
      texto: 'Este sitio. Sigue las corridas en vivo y guarda el historial. Por diseño no manda ' +
        'órdenes al equipo: arrancar un experimento a distancia, sin nadie en la sala, no es una opción.',
    },
  ],
  autor: {
    nombre: 'Mario Ariel Gómez',
    rol: 'Proyecto Final de Bioingeniería · FIUNER · 2026',
    bio:
      'Fui becario en el proyecto de investigación sobre los efectos de los campos ' +
      'electromagnéticos de baja frecuencia en la memoria y el aprendizaje, en el ' + LACEMAB +
      ' de la Facultad de Ingeniería de la UNER. Ahí operaba a mano el generador de campo con el ' +
      'que se hacían las exposiciones. Este sistema automatiza ese proceso, y es mi proyecto ' +
      'final de la carrera de Bioingeniería.',
    correo: 'gomez.92m@gmail.com',
    enlaces: [
      { texto: 'LinkedIn', url: 'https://www.linkedin.com/in/mario-ariel-gomez-2aba52259' },
      { texto: 'Código fuente en GitHub', url: 'https://github.com/gomez-92/BioSoft' },
    ] as Array<{ texto: string; url: string }>,
  },
  // Direccion y marco del trabajo.
  equipo: [
    { rol: 'Director', nombre: 'Esp. Bioing. Alejandro Faust' },
    { rol: 'Codirector', nombre: 'Bioing. Silvio Lauguero' },
    { rol: 'Laboratorio', nombre: LACEMAB + ', Facultad de Ingeniería, UNER' },
    { rol: 'Proyecto', nombre: 'Efectos de los campos electromagnéticos de baja frecuencia en la memoria y el aprendizaje' },
  ] as Array<{ rol: string; nombre: string }>,
  facultad: {
    nombre: 'Facultad de Ingeniería',
    universidad: 'Universidad Nacional de Entre Ríos',
    lugar: 'Oro Verde, Entre Ríos, Argentina',
    carrera: 'Bioingeniería',
    texto: 'Desarrollado en el LACEMAB de la Facultad de Ingeniería de la Universidad Nacional ' +
      'de Entre Ríos (Oro Verde, Entre Ríos).',
    url: 'https://ingenieria.uner.edu.ar/',
  },
};

export function Acerca({ publica = false }: { publica?: boolean }) {
  const { proyecto, piezas, autor, equipo, facultad } = ACERCA;
  return (
    <div className="acerca">
      {/* Sin sesion no hay menu lateral (es parte del sistema, no de esta
          pagina): solo la marca y el boton para entrar. */}
      {publica && (
        <header className="acerca-barra">
          <span className="marca">
            <span className="marca-logo" aria-hidden="true"><Icono nombre="campo" /></span>
            <span>
              <span className="marca-nombre">SEMBAF</span>
              <span className="subtitle marca-sub">Monitor remoto</span>
            </span>
          </span>
          <a href="/" className="boton boton-primario acerca-entrar">Iniciar sesión</a>
        </header>
      )}

      <section className="panel acerca-portada">
        <span className="marca-logo acerca-logo" aria-hidden="true"><Icono nombre="campo" tamaño={26} /></span>
        <p className="acerca-sigla">{proyecto.sigla}</p>
        <h2 className="acerca-titulo">{proyecto.nombre}</h2>
        <p className="acerca-lead">{proyecto.resumen}</p>
      </section>

      <section className="panel">
        <h3>Cómo está armado</h3>
        <div className="acerca-piezas">
          {piezas.map((p) => (
            <article key={p.titulo} className="acerca-pieza">
              <span className="acerca-pieza-icono"><Icono nombre={p.icono} tamaño={18} /></span>
              <h4>{p.titulo}</h4>
              <p>{p.texto}</p>
            </article>
          ))}
        </div>
        <p className="acerca-nota">{proyecto.grupos}</p>
        {proyecto.desarrollo && <p className="acerca-nota">{proyecto.desarrollo}</p>}
      </section>

      <div className="columnas-2">
        <section className="panel">
          <h3><Icono nombre="cuenta" /> El autor</h3>
          <p className="acerca-nombre">{autor.nombre}</p>
          <p className="tenue acerca-rol">{autor.rol}</p>
          {autor.bio && <p className="acerca-texto">{autor.bio}</p>}
          {(autor.correo || autor.enlaces.length > 0) && (
            <ul className="acerca-enlaces">
              {autor.correo && <li><a href={`mailto:${autor.correo}`}>{autor.correo}</a></li>}
              {autor.enlaces.map((e) => (
                <li key={e.url}><a href={e.url} target="_blank" rel="noreferrer">{e.texto} ↗</a></li>
              ))}
            </ul>
          )}
        </section>

        <section className="panel">
          <h3><Icono nombre="facultad" /> La facultad</h3>
          <p className="acerca-nombre">{facultad.nombre}</p>
          <p className="tenue acerca-rol">{facultad.universidad}</p>
          <dl className="datos acerca-equipo">
            <dt>Carrera</dt><dd>{facultad.carrera}</dd>
            <dt>Lugar</dt><dd>{facultad.lugar}</dd>
          </dl>
          {facultad.texto && <p className="acerca-texto">{facultad.texto}</p>}
          {facultad.url && (
            <p className="acerca-texto">
              <a href={facultad.url} target="_blank" rel="noreferrer">Sitio de la facultad ↗</a>
            </p>
          )}
        </section>
      </div>

      {equipo.length > 0 && (
        <section className="panel">
          <h3><Icono nombre="usuarios" /> Dirección</h3>
          <dl className="datos">
            {equipo.map((m) => (
              <div key={m.rol} className="acerca-equipo-fila"><dt>{m.rol}</dt><dd>{m.nombre}</dd></div>
            ))}
          </dl>
        </section>
      )}
    </div>
  );
}
