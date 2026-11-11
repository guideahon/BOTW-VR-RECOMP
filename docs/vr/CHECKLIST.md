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
- [ ] **Hoy la flecha sale hacia donde mirás con la cabeza**, no hacia donde apunta el arco (eso falta, ver abajo).
      Apuntá con la mirada.

## 5. Combate

- [ ] Un tajo con la mano derecha ataca una vez; una estocada, una vez; volver lento a la guardia no ataca.
- [ ] Mano izquierda levantada delante de la cara: cubre con el escudo.
- [ ] Probá `SAVAGEWIND_VR_SWING=easy` si cuesta que cuenten los tajos.

## 6. Vibración del juego (nuevo)

- [ ] Las vibraciones del juego (golpes recibidos, impactos, cargar el arco, explosiones) llegan al control de cada
      mano. Si es muy fuerte o débil: `SAVAGEWIND_VR_RUMBLE_GAIN` (1 por defecto); `SAVAGEWIND_VR_RUMBLE=0` la apaga.
      (Probado: el camino hasta el control; no probado: qué eventos del juego vibran.)

## 7. Comodidad y movimiento

- [ ] Giro suave y por pasos (`SAVAGEWIND_VR_TURN=snap`, `SAVAGEWIND_VR_SNAP_DEGREES`) funcionan y no marean.
- [ ] `SAVAGEWIND_VR_TURN_SPEED` cambia la velocidad del giro suave.
- [ ] Caminar hacia donde mirás (por defecto) o hacia donde apunta el control izquierdo (`SAVAGEWIND_VR_WALK=controller`).
- [ ] Modo zurdo (`SAVAGEWIND_VR_LEFT_HANDED=1`): los papeles de las manos se invierten.
- [ ] Sentado: recentrar (`vr recenter` en la consola) te pone a la altura de Link.
- [ ] Tercera persona (`SAVAGEWIND_VR_VIEW=third`): la cámara del juego detrás de Link, con la cabeza encima.

## Lo que todavía no está (de BetterVR)

| Qué | Por qué falta |
|---|---|
| Que el arma pegue donde la movés (su hitbox siguiendo la mano) | hay que encontrar en el juego el "sensor de ataque" del arma |
| Arco apuntado con las manos (la flecha sale hacia donde apunta el arco) | probado desviar la cámara lógica: el juego apunta con la misma matriz que se dibuja; falta encontrar el disparo de la flecha (notas en docs/vr/re-notes.md) |
| Soltar el arma con un gesto (mano izquierda en la cadera derecha) | el juego no tiene botón para eso; hay que encontrar su función |
| Agarrar armas de dos manos con ambas manos | depende del punto anterior y de los brazos |
| Modos de cinemáticas (primera persona en escenas) | hoy las escenas van a la pantalla virtual |
| Cuerpo que sigue tus pasos reales (roomscale físico) | hay que mover a Link con su física |
| Menú de opciones VR dentro del juego | hoy son variables de entorno (el instalador las escribe) |
