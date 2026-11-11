# BOTW VR: lista de verificación con el visor

Todo lo de esta lista está probado automáticamente contra `fake_xr` (un runtime OpenXR simulado, con la cabeza y las
manos guionadas) en el juego real, salvo donde dice lo contrario. Lo que una prueba así **no puede** juzgar es cómo se
siente y se ve con un visor puesto. Marcá cada punto en tu Quest 3 (Virtual Desktop, VDXR) y anotá lo que no ande.

Arrancá con `Play BOTW VR.bat` (o `BOTW VR.exe`). Primera persona es la vista por defecto.

## 1. Imagen y cabeza

- [ ] El juego aparece en estéreo (profundidad real, sin ojos cruzados ni invertidos).
- [ ] Mover la cabeza en cualquier dirección mueve la vista sin retraso perceptible.
- [ ] Al mirar abajo no ves la cara, el pelo ni el gorro de Link (cabeza oculta).
- [ ] Al mirar abajo ves el cuerpo de Link debajo tuyo, a una altura creíble (si no: `SAVAGEWIND_VR_EYE_HEIGHT`, por
      defecto 1.55, y `SAVAGEWIND_VR_EYE_FORWARD`, 0.18).
- [ ] El HUD (corazones, mapa) flota delante y se lee bien.
- [ ] Menús de pausa, mapa e inventario aparecen en la pantalla virtual y se vuelve al mundo al cerrarlos.
- [ ] Anotá los "bugs visuales" que veas (parpadeos, sombras distintas por ojo, objetos que tiemblan): qué, dónde y en
      qué momento.
- [ ] Rendimiento: ¿va fluido? Probá `SAVAGEWIND_VR_HEIGHT` 1080 / 1440 / 1800 y la frecuencia de Virtual Desktop
      (72 / 80 / 90 Hz).

## 2. Brazos y manos (nuevo)

- [ ] Los brazos de Link siguen tus controles; las manos quedan donde están tus manos.
- [ ] El codo dobla hacia abajo y afuera de forma natural (no hacia adentro ni hacia arriba).
- [ ] La mano apunta hacia donde apunta el control. **Probable ajuste:** el giro de la muñeca sobre su eje lo pone la
      animación; decime si la mano queda torcida.
- [ ] Con un arma desenvainada, el arma está en la mano derecha y la sigue.
- [ ] Mangas y guantes de la armadura siguen al brazo.
- [ ] Con los brazos estirados al máximo no se rompe nada (se estiran un poco, como en BetterVR).

## 3. Controles de BetterVR

- [ ] Gatillo derecho: ataca con el arma en la mano, tensa el arco si tenés el arco, usa la runa.
- [ ] B derecho: salta. A derecho: agarra / interactúa; mantenido: corre.
- [ ] Y izquierdo: saca la runa; mantenido: menú de runas.
- [ ] Clic stick izquierdo: agacharse; mantenido: catalejo. Clic stick derecho: inventario; mantenido: mapa.
- [ ] Gatillo izquierdo: fijar objetivo / escudo.

## 4. Gestos con el cuerpo

- [ ] Grip (apretar y soltar) con la mano sobre el hombro derecho: saca el arma; otra vez: la guarda.
- [ ] Hombro izquierdo: el arco. Cadera izquierda: la runa.
- [ ] Grip mantenido medio segundo sobre esos lugares: abre el menú rápido; al soltar se equipa lo elegido.
- [ ] Gatillo derecho mantenido con la mano sobre el hombro y el arma afuera: al soltar, la arroja.
- [ ] Los dos grips en la boca: silbido (viene el caballo).
- [ ] Empujón rápido con la mano izquierda mientras cubrís con el escudo: parry.
- [ ] Cada lugar del cuerpo vibra al llegar con la mano, y cada gesto vibra al dispararse.
- [ ] **Magnesis (sólo probado en pruebas unitarias):** con Magnesis activa y un objeto metálico agarrado, grip
      mantenido: subir / bajar la mano lo sube / baja; empujar / tirar hacia vos lo aleja / acerca.

## 4b. Arco

- [ ] Grip sobre el hombro izquierdo: el arco aparece en la mano izquierda y la sigue.
- [ ] Gatillo derecho mantenido con el arco afuera: tensa; la flecha aparece en la mano derecha. Al soltar, dispara.
- [ ] **La flecha sale hacia donde apunta el arco**: en la línea que va de la mano que tensa (derecha) a la mano del
      arco (izquierda), aunque mires a otro lado. Probá apuntar con el arco a un costado sin girar la cabeza.
- [ ] Con las manos juntas (sin tensar lejos), la flecha sale hacia donde apunta el control del arco.
- [ ] Si preferís apuntar con la mirada: `SAVAGEWIND_VR_BOW_AIM=head`.
- [ ] Mientras tensás (flecha puesta) se ve la trayectoria como puntos amarillos que salen del arco: recta unos
      25 m y después cae. Al soltar el gatillo (disparo) desaparecen. Se dibujan encima de todo (no se esconden detrás de una
      pared) y la curva es aproximada (no depende del arco que uses): decime si la flecha cae muy lejos de los puntos.
- [ ] Los puntos se ven en los dos ojos en el mismo lugar (sin doble imagen).
- [ ] Para sacarlos: `SAVAGEWIND_VR_BOW_ARC=0`.

## 4b2. Espadones y lanzas a dos manos (nuevo)

- [ ] Con un espadón o una lanza en la mano derecha y nada en la izquierda: acercá la mano izquierda a la hoja / al
      mango (sobre la línea del arma, hasta ~25 cm de ella) y apretá el grip izquierdo: queda agarrada con las dos
      manos mientras lo mantengas. Ahora el arma apunta de tu mano derecha hacia la izquierda: moviendo la izquierda la
      dirigís (como un bastón).
- [ ] Ese grip no agarra objetos del mundo (no hace "A").
- [ ] Los golpes a dos manos pegan más fuerte (x1.2); con una sola mano un poco menos (x0.85), como en BetterVR.
- [ ] Ojo: la mano izquierda de Link va a tu control, no queda clavada en el mango; si se ve muy separada, avisame.
- [ ] Para apagarlo: `SAVAGEWIND_VR_TWO_HANDS=0`.

## 4c. Soltar (nuevo)

- [ ] Con la espada en la mano derecha: llevá la mano derecha a la cadera derecha y mantené el grip medio segundo. Se
      abre un instante el menú rápido de armas y la espada cae al piso delante tuyo (y sale de tu inventario, como el
      "soltar" del juego). La podés volver a agarrar con el grip.
- [ ] Con el escudo: llevá la mano **izquierda** cruzada hasta la cadera derecha y mantené el grip: suelta el escudo.
- [ ] Con el arco en la mano izquierda: cruzá esa mano hasta la cadera derecha y mantené el grip: el arco cae al piso.
- [ ] Con la runa activa, el mismo gesto no descarta la Sheikah Slate.
- [ ] Un toque corto del grip en la cadera derecha no hace nada (ni agarra).
- [ ] Si no lo querés: `SAVAGEWIND_VR_DROP=0`.

## 4f. Caminar por la habitación (nuevo)

- [ ] En primera persona, dá un paso real hacia un costado o adelante: Link camina hasta quedar debajo tuyo (lo ves
      moverse y se oyen sus pasos). La vista no salta: queda donde está tu cabeza.
- [ ] Contra una pared o un borde Link se frena como siempre (el juego maneja los choques); tu cabeza puede seguir y
      atravesar la pared (como antes).
- [ ] Si mareás o no lo querés: Opciones > RV > Escala de habitación (apagado), o `SAVAGEWIND_VR_ROOMSCALE=0`.
- [ ] Con el stick empujado, el stick manda (los pasos reales no lo mueven mientras tanto).

## 4e. Opciones de RV en el menú del juego (nuevo)

- [ ] Menú de pausa > Sistema > Opciones: hay una sección **RV** (también desde las Opciones de la pantalla de título).
- [ ] Adentro: Vista, Cinemáticas, Giro (suave / por pasos / solo cabeza), Ángulo del paso, Velocidad de giro, Dirección al caminar,
      Zurdo, Vibración y su fuerza, Combate con movimiento, Tajos fáciles, Los tajos hacen el ataque de Link, Pegar con
      el movimiento, Golpe donde está tu mano, Levantar la mano izquierda para cubrirse, Agarre a dos manos, Gesto de
      soltar, Apuntar el arco con las manos, Trayectoria del arco. Los textos en español.
- [ ] Cambiar una opción se nota enseguida (por ejemplo Vista: tercera persona, o Giro: por pasos) y queda guardada al
      volver a abrir el juego.
- [ ] Ojo: si el lanzador pone alguna variable `SAVAGEWIND_VR_*`, esa opción manda sobre el menú. El instalador nuevo
      no debería ponerlas; si una opción del menú "no hace nada", avisame.

## 4d. Cinemáticas (nuevo)

Ahora las escenas ya no van a la pantalla plana por defecto: se ven en 3D como en BetterVR, que trae una tabla con
~1500 escenas que dice cuáles van en primera persona y cuáles con la cámara de la escena.

- [ ] Abrir un cofre / cocinar / agarrar algo nuevo (escenas de la tabla en primera persona): las ves desde los ojos de
      Link, y la vista no se gira sola con la cámara de la escena.
- [ ] Una escena con cámara (charla con un personaje, una escena de historia): la ves en 3D desde la cámara de la escena
      y podés mirar alrededor con la cabeza. Si marea, probá las otras opciones:
- [ ] `SAVAGEWIND_VR_EVENTS=screen`: todas las escenas en la pantalla plana (como antes).
- [ ] `SAVAGEWIND_VR_EVENTS=third`: todas con la cámara de la escena; `=first`: todas en primera persona.
- [ ] Decime qué escenas se ven mal en el modo `auto` (nombre aproximado o qué pasaba), así ajusto la tabla.

## 5. Combate

- [ ] Un tajo con la mano derecha ataca una vez; una estocada, una vez; volver lento a la guardia no ataca.
- [ ] Mano izquierda levantada delante de la cara: cubre con el escudo.
- [ ] Probá `SAVAGEWIND_VR_SWING=easy` si cuesta que cuenten los tajos.
- [ ] **El golpe sale donde está tu mano (nuevo):** con la espada en la mano derecha, poné el brazo a un costado y
      hacé un tajo hacia un enemigo o un arbusto que esté de ese lado: tiene que pegarle / cortarlo, aunque Link
      "en el juego" mire hacia adelante. Antes el golpe salía donde la animación ponía la espada.
- [ ] **Pegás con cualquier movimiento (nuevo):** un tajo rápido con la espada pega donde pase la hoja; un roce lento
      casi no hace daño (x0.2), un tajo con todo pega fuerte (x1.35). Cada tajo pega una vez a cada enemigo.
- [ ] Tajos contra un árbol: lo cortan.
- [ ] Apoyar la espada quieta sobre un enemigo no le hace nada.
- [ ] Probá `SAVAGEWIND_VR_SWING_ATTACK=0`: el tajo ya no dispara la animación de ataque de Link (como en BetterVR),
      solo pega. Decime cuál te gusta más.
- [ ] Si los golpes se sienten raros: `SAVAGEWIND_VR_SWING_HITS=0` deja solo los ataques del juego.
- [ ] Si algo de la física del arma se ve raro (escudo, arco, la espada temblando o atravesando cosas):
      `SAVAGEWIND_VR_WEAPON_HITS=0` lo deja como antes; avisame.

## 6. Vibración del juego (nuevo)

- [ ] Las vibraciones del juego (golpes recibidos, impactos, cargar el arco, explosiones) llegan al control de cada
      mano. Si es muy fuerte o débil: `SAVAGEWIND_VR_RUMBLE_GAIN` (1 por defecto); `SAVAGEWIND_VR_RUMBLE=0` la apaga.
      (Probado: el camino hasta el control; no probado: qué eventos del juego vibran.)

## 7. Comodidad y movimiento

- [ ] Giro suave y por pasos (`SAVAGEWIND_VR_TURN=snap`, `SAVAGEWIND_VR_SNAP_DEGREES`) funcionan y no marean.
- [ ] Giro "Solo cabeza" (`SAVAGEWIND_VR_TURN=head`): en primera persona, el stick derecho no gira la vista de lado;
      al girar tu cabeza sí gira. La dirección vertical del stick sigue disponible para apuntar y Magnesis.
- [ ] `SAVAGEWIND_VR_TURN_SPEED` cambia la velocidad del giro suave.
- [ ] Caminar hacia donde mirás (por defecto) o hacia donde apunta el control izquierdo (`SAVAGEWIND_VR_WALK=controller`).
- [ ] Modo zurdo (`SAVAGEWIND_VR_LEFT_HANDED=1`): los papeles de las manos se invierten.
- [ ] Sentado: recentrar (`vr recenter` en la consola) te pone a la altura de Link.
- [ ] Tercera persona (`SAVAGEWIND_VR_VIEW=third`): la cámara del juego detrás de Link, con la cabeza encima.

## Verificación pendiente

Los gestos anteriores y la comodidad visual todavía necesitan una prueba en el Quest. BetterVR tampoco permite
descartar la Sheikah Slate: el gesto de la cadera no la suelta.
