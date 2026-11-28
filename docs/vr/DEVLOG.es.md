# BOTW VR: bitácora de desarrollo

*In English (shorter): [DEVLOG.md](DEVLOG.md)*

Registro de lo hecho, cómo se probó y lo que queda. Lo más reciente arriba. Los detalles técnicos de cada cambio están
en [../VR.md](../VR.md) (variables, tests) y la ingeniería inversa en [re-notes.md](re-notes.md); lo que hay que
probar con el visor puesto, en [CHECKLIST.es.md](CHECKLIST.es.md); el estado de cada feature, en
[FEATURES.es.md](FEATURES.es.md).

## 2026-11-28 (el test del catálogo de AppImage)

- **"Could not find a single window on screen":** la máquina del test del catálogo de AppImage no tiene driver Vulkan,
  así que el programa se detiene al crear la ventana y muestra su cuadro de error; pero el cuadro X11 de SDL, con el
  locale UTF-8 del runner, nombra su ventana en texto "UTF-8", que el xdotool del catálogo (2016) no sabe leer: no
  contaba ninguna ventana. Reproducido en un runner de GitHub (`.github/workflows/catalog-test.yml`: `WM_NAME(UTF-8)`)
  y en un contenedor como él; un cuadro de error todo ASCII ahora se muestra con el locale C (su nombre en texto plano)
  y dice que no se encontró un driver Vulkan. `tools/linux/test_catalog_window.sh`: el AppImage de 26.2 falla, el nuevo
  muestra su ventana en 1 s.
- **28.1, todavía "ni una ventana":** su xdotool ya encontraba el cuadro, pero el catálogo después cuenta solo las
  ventanas que `xwininfo -tree` lista con clase, y el cuadro de mensaje X11 de SDL no pone WM_CLASS. Si todavía no hay
  ninguna ventana nuestra, ahora hay una simple (una superficie de ventana: sin Vulkan, sin GPU) debajo del cuadro, con
  la clase de SDL. Los dos tests del catálogo cuentan así también: 28.1 los falla (como en el catálogo), 28.2 los pasa;
  con Vulkan (lavapipe) la pantalla de inicio abre como antes.
- **28.2, "la captura muestra un mensaje de error":** el catálogo también lee su captura de pantalla, y la nuestra
  era el cuadro de error. El AppImage x86_64 ahora lleva el Vulkan por software de Mesa (lavapipe, el de Ubuntu 22.04,
  con LLVM y las bibliotecas que necesita: usr/lib/lavapipe, +46 MB); sin driver Vulkan el programa vuelve a arrancar
  una vez sobre él (device.c `software_vulkan_retry`: no se puede crear la ventana o la instancia, o ningún driver lista
  una GPU), y la pantalla de inicio abre con un aviso arriba de que el juego en sí necesita un driver
  (`SAVAGEWIND_NO_SOFTWARE_VULKAN=1`: el cuadro de error como antes). En el runner del catálogo: la pantalla de inicio
  en 1 s sobre llvmpipe; la RTX 3090 de esta PC se sigue eligiendo como antes. Acreditado en docs/third-party.md, la
  licencia de cada paquete en usr/lib/lavapipe/licenses/.
- **El chequeo i18n del CI** fallaba desde 26.2: la línea de producto de los logs (`rt_product`) se leía como tres
  textos de menú; marcada `i18n-skip`.
- **Nombres:** los archivos de la release son `BOTW-VR-Recomp-<versión>-...` sin "Linux" (`-x86_64.AppImage`,
  `-SteamFrame-aarch64.AppImage`, `-Windows.zip`), y el archivo .desktop del AppImage dice BOTW VR Recomp: el nombre
  del proyecto, como el repositorio y el README, que el catálogo compara.

## 2026-11-27 (las otras flechas de los arcos múltiples; atacar con "cuatro brazos")

- **Las flechas extra de un arco de centaleón iban adonde miraba la cabeza** (la principal, por el arco): tienen su
  propio paso de disparo, main+0x92ce7c (los mismos FirstSpeed / TargetPos / IsShootByPlayer que la principal, con la
  dispersión en el objetivo). Un sitio nuevo después de su dirección (`arrow_aim_side`, main+0x92cfbc) les da el mismo
  giro que a la principal (de la puntería del juego a la de las manos), y la dispersión de 3° queda alrededor del arco.
  Arco de centaleón salvaje (Weapon_Bow_032), manos a -54°, cabeza a 0: antes -0,4 a -4°; ahora las cinco entre -49 y
  -53. `tools/vr/save_equip.py` arma un save con ese arco equipado; `test_vr_bow.py --others 2` revisa cada flecha; caso
  de regresión `bow-multishot`.
- **Al atacar, "como si Link tuviera 4 brazos":** el tajo además apretaba el ataque del juego, cuya animación movía el
  cuerpo y los hombros de Link debajo de los brazos que siguen a los controles. Opciones > VR > Los tajos hacen el ataque
  de Link ahora viene apagado, como BetterVR (el tajo pega con el sensor del arma; clave nueva `vr_swing_anim`, para no
  leer al revés un valor guardado). Las mangas y guantes se midieron sobre los brazos durante los ataques (1088
  mediciones, 0 desfasadas: no eran la causa). `test_vr_attack_arms.py` (`--swing-attack 1`: el control negativo); caso
  de regresión `attack-no-anim`.

- **Al caminar "titila todo el cuerpo" (Stereo):** cada ojo es un cuadro del juego medio paso aparte y los dos se dibujan
  desde el lugar del izquierdo, pero el cuadro derecho volvía a animar a Link: el torso, el cinturón y las piernas estaban
  en otro lugar en cada ojo, mientras los brazos (los controles, guardados una vez por par) coincidían. Ahora el cuadro
  izquierdo guarda los huesos de Link y de los modelos que lleva (armadura, armas) y el derecho del mismo par dibuja esos
  (vr_skel.c `pair_pose`; `SAVAGEWIND_VR_PAIR_POSE=0` lo apaga; `pair_poses_kept` en el comando `vr`).
  `test_vr_body_walk.py` (ojos a 0 m, mirando abajo, caminando): la parte de abajo distinta 19-34% -> 2-9% (el pasto y la
  luz; 1,6% quieto); caso de regresión `body-walk-stereo`.
- **La luz de una antorcha de un lado en un ojo y del otro en el otro (Stereo):** no era un parpadeo: cada ojo mantenía
  su propia luz captura tras captura (izquierdo contra izquierdo 0,5%, izquierdo contra derecho 4,7%, quieto en Kakariko
  de noche), y con campos simétricos (`SAVAGEWIND_VR_ASYMMETRIC=0`) los dos ojos eran una sola imagen (0,03%). Las luces
  locales (vertex shader 72cb759a00, un dibujo por luz) arman el rayo de vista de cada píxel con el campo en cb3 +0xd0
  (tan x, -tan y, centro x, centro y); el juego deja el centro en 0 y el campo de cada ojo está descentrado, así que la
  luz caía en otro lugar de la pantalla, para un lado en cada ojo. exec.c `cbuf_info` le da a ese bloque (reconocido por
  contenido) el centro del ojo, en menos sus tangentes (el signo medido contra la imagen simétrica). Kakariko de noche
  2,2-7,6% -> 0,5-1,2% entre los ojos, Lurelin 0,6-33% -> 0,4-2,3% (lo que queda, en los bordes, junto a la llama de una
  antorcha). `SAVAGEWIND_VR_RAY_FIX=0`: como lo hace el juego; `rays_fixed` en el comando `vr`; caso de regresión
  `fire-light-stereo`. Tampoco era el generador de números aleatorios (darle al cuadro derecho los del izquierdo,
  sead::Random::getU32 main+0xeb6e7c: probado, sin cambio, sacado).
- **"Con Rendimiento encendido el árbol de la prueba de trepar no está": no faltaba nada, fallaba la prueba.** Las fotos
  del lugar (fake_xr, cuatro lados, Rendimiento encendido y apagado, y con los árboles lejanos del terreno alejados con
  `SAVAGEWIND_TREE_NEAR=8`) muestran el mismo árbol y las mismas rocas, y lo que Link agarra ahí es una pared de roca,
  no el árbol. Diez caminatas con el mismo yaw de cabeza en una corrida: trepa 2 de 10 con Rendimiento encendido y 2 de
  10 apagado, en el mismo lugar (~-963 1884); su rumbo se corre decenas de grados de un `goto` al siguiente (aun fijando
  el yaw de la cámara con `goto swr:1 ... yaw=`), cada corrida distinto (el tiempo; lo más probable, los fps, que
  Rendimiento cambia: no medido). Los ocho yaws fijos de la prueba erraban la pared en corridas enteras, más seguido con
  Rendimiento encendido. Las bisecciones de la distancia de dibujado (`SAVAGEWIND_TRAVERSE_SCALE` 0,67, 0,75, 0,9, 1 y
  1,2 pasaron; `SAVAGEWIND_NEAR_MIN` 160-450 a veces sí y a veces no, 450 una vez cada cosa; el caso base pasó 1 de 2)
  solo movían esa probabilidad. Las distancias de recorrido que pasan por `rt_traverse_scale` ahí empiezan en 40 m (24 m
  en Potato), lejos de los 10 m de la prueba. `test_vr_climb.py` ahora empieza a 6 m de la parte alta de la pared
  (-958.5 233 1885.0) y la dirige: cada 0,4 s gira el yaw lo que sus últimos pasos se desviaron de la línea a la pared
  (desde el árbol esa línea cruzaba una roca de ~2 m a la que se subía antes de la muestra "trepando"; así, 6 de 6
  trepaban igual). Al primer intento siempre: Rendimiento encendido 4 de 4 (3 por `regress.py`), apagado 1 de 1; los
  ojos colgado y trepando, todos dentro de lo pedido (0,08 m sobre la cabeza, 0,09-0,15 m de ella). `climb-eye` vuelve a
  correr con Rendimiento encendido; la distancia de dibujado no cambia (no hay fps que medir).

## 2026-11-26 (el tester de 26.2: manos cruzadas al moverlas, rotación en tercera persona; los dos reproducidos y arreglados)

- **Manos cruzadas al moverlas (Estéreo, primera persona):** la cabeza se ubicaba una vez por par (2026-11-24), los
  controles en cada cuadro del juego. En un visor el cuadro del ojo derecho los ubica más tarde, y la predicción del
  runtime para el mismo instante de pantalla ya avanzó con tracking nuevo: las manos en movimiento se dibujaban en otro
  lugar en cada ojo. fake_xr daba la misma pose para el mismo instante, y las pruebas de manos medían un campo
  (tangentes hasta 0,6) en el que las manos, 25 cm debajo de los ojos, no estaban: las dos cosas lo escondían. Ahora el
  cuadro izquierdo del par ubica los controles y el derecho dibuja con esos (vr_camera.c `vr_pair_hand`; `pair_hands_kept`
  en el comando `vr`). `test_vr_hands_moving.py` (nuevo; `FAKE_XR_HAND_DRIFT` en fake_xr hace que una ubicación más
  tardía vea una pose más tardía, como un runtime): antes, las manos a 3-12 muestras entre ojos (hasta ~3,4°); después, 0.
  `test_vr_stereo_sync.py --walk` ahora pone las manos donde mide.
- **Rotación en tercera persona:** con la cabeza girada, el juego devuelve nuestra propia cámara con el punto "at" a
  otra distancia; no reconocida como nuestra, recibía de nuevo el giro de la cabeza: la vista alternaba entre la
  dirección del juego más el giro y más el doble, de cuadro a cuadro y de ojo a ojo, y el ojo derecho tomaba la doble
  del ancla del par. Las pruebas tenían la cabeza al frente (el doble de 0 es 0). La nuestra se reconoce ahora por el
  ojo y la dirección, contra las ocho últimas escritas: la vista fuera de "juego + cabeza" en el 22,3% de las
  actualizaciones antes, 0 después.
- **Escalada (P10):** el ojo quedaba 0,38 m sobre la cabeza de Link y 0,32 m atrás mientras colgaba del árbol del
  Plateau (`eye_above_head` / `eye_from_head_flat`, nuevos en el comando `vr`): se corría desde sus pies a altura de
  parado. Ahora desde el hueso de su cabeza, a 0,12 m de la pared: 0,08 m arriba, 0,10-0,14 m atrás (`test_vr_climb.py`).
- **A derecho en una pared (esquema Quest):** soltaba (la B del juego, esquema de BetterVR), así que "A Coger" en una
  manzana tiraba a Link del árbol; en el esquema Quest A sigue siendo A trepando, B suelta (`test_vr_gestures`, 180).
- **La suite de regresión** (`tools/vr/regress.py`, TESTING.es.md): cada prueba que encontró un error, antes de cada release.

## 2026-11-26 (Opciones > VR > Rendimiento; unos minutos por modo; el reporte del tester de 25.9 revisado)

`tools/vr/bench/quick.py`: un arranque por modo (~2 min, fake_xr, los ojos a 0 m): fps del visor, los dos ojos en dos
direcciones, girando y justo después de cerrar un menú. Cada cosa que cambian los "mods de rendimiento" (los packs de
Cemu; Savage Wind ya tiene los fps dinámicos de FPS++, framepace.c), sola en Stereo (base 104-106 fps, ruido +-3%):

| Opción | fps | Ganancia | Imagen |
|---|---|---|---|
| Distancia de dibujado Potato | 116 | +11% | igual de cerca; menos actores a lo lejos |
| SMAA en lugar de TAA | 111 | +6% | algo más de serrucho |
| Sin hierba | 112 | +7% (nada sobre las dos de arriba) | sin hierba: descartada |
| Sombras Potato | 103 | 0% | descartada |

**Opciones > VR > Rendimiento** (encendido por defecto, `vr_performance`, `SAVAGEWIND_VR_PERFORMANCE=0` lo apaga): en
el visor la distancia es Potato y TAA pasa a SMAA (draw_distance.c `level`, output_scale.c `rt_antialias`); lo de
Video no se toca. Con la configuración por defecto: Stereo 106,5 -> 133,6 fps del visor, Multiview 136 -> 169, Mono
185 -> 243; todos los chequeos pasan en cada uno (ojos iguales, girando, el menú).

El reporte del tester de 25.9: tercera persona, la vista contra la cámara del juego girando, en 25.9: ojo izquierdo
0,4° (máx. 1,6), derecho 1,6 (máx. 3,1); ahora izquierdo 0,0, derecho 0,8 (máx. 1,6) - el derecho dibujado con el
ancla del par, a propósito (los dos ojos desde un lugar: 0 px entre ellos girando y caminando, `test_vr_stereo_sync.py
--view third [--walk]`). Ojos después de cerrar el inventario (`test_vr_menu_flicker.py`): sin diferencia en Stereo
ni en 25.9 ni ahora; un parpadeo que necesite cuadros perdidos queda fuera de fake_xr: hacen falta el log del tester y
su modo de renderizado. Sync (el prototipo oculto) sigue dibujando los ojos distintos (9-14%, hasta 100% tras un menú).

**Savage Wind Configuration antes del juego, también en Windows:** el zip VR no trae `sw-manifest.txt` (no tiene SW Update), y en Windows un arranque normal abría la configuración solo en una release con ese archivo: la build VR de Windows entraba directo al visor mientras los AppImage mostraban la configuración primero. En una build VR ahora aparece primero en todas las plataformas (main.c); el `play-vr.bat` de la build de fuentes y el `Play BOTW VR.bat` del instalador ya no pasan `--play`. Linux comprobado con `test_vr_live_launcher.py` (PASS); Windows sin correr acá. La entrada de Steam "Savage Wind" (`--game`) sigue entrando directo; "Savage Wind Configuration" la abre.

**Versión propia de BOTW VR:** `runtime/vr/botwvr_version.h` (`alpha-2026.11.26.2`, la pantalla de inicio: "Versión alpha 2026.11.26.2"); antes era la de Savage Wind, 0.6.0-alpha.6. Los archivos del release son `BOTW-VR-<versión>-...`; el workflow de release compara el tag con ella. La versión de Savage Wind aparece al lado en los logs.

## 2026-11-26 (los dos ojos comparados en cada modo de renderizado, cuatro lugares)

`test_vr_eye_match.py` (fake_xr, los ojos a 0 m: las dos imágenes deben ser una; la parte que difiere en más de 24
niveles) en Kakariko (humo de chimeneas, antorchas), la Torre de Hebra (nieve, vista lejana), la Ciudadela Gerudo
(arena brillante, antorchas) y Lurelin (mar, antorchas), cuatro direcciones en cada uno:

| Modo | Kakariko | Torre de Hebra | Ciudadela Gerudo | Lurelin |
|---|---|---|---|---|
| Estéreo (por defecto) | 2,3-3,8% | 0,3-1,2% | 0,0-3,9% | 1,9-2,7%, **22% a 90 grados** |
| Multiview | 0,00-0,16% | 0,00% | 0,00% | 0,00-0,02% |
| Sync (prototipo, `SAVAGEWIND_VR_SYNC=1`) | hasta 97% | hasta 24% | hasta 69% | hasta 94% |

- **Multiview pasa en todos lados**, también de noche con lluvia en Kakariko (0,00-0,02%), y las manos caminando de
  costado son iguales en los dos ojos (diferencia 0,0). Ahora es el modo en que los dos ojos ven un mismo mundo.
- **Las diferencias de Estéreo son la luz de las antorchas**: cada ojo es un cuadro distinto del juego y la luz del
  fuego parpadea y se mueve entre cuadros, así que un ojo ve una roca o una pared iluminada en naranja y el otro no (la
  estatua del santuario de Lurelin, las paredes de la Ciudadela Gerudo). Es propio de un cuadro del juego por ojo; en el
  visor, una luz que titila distinto en cada ojo cerca del fuego. Multiview no lo tiene.
- **Sync (el prototipo, no está en el menú)** pierde gran parte del terreno en el ojo derecho: también en un build de
  antes de los cambios de esta semana (`bin/sync3`). Darle a su pasada derecha el bloque de constantes entero (como el
  arreglo del terreno de multiview) mejoró dos de cuatro direcciones en Lurelin; el resto queda: multiview lo reemplaza.
- No es un bug: las **líneas rojas sobre el terreno lejano** vistas desde la Torre de Hebra y la Meseta son los rayos de
  las Bestias Divinas al Castillo de Hyrule (la partida de prueba está al 100%).
- **Rendimiento** (`tools/vr/bench/bench.py`, fake_xr sin límite, Meseta de los Albores, RTX 3090, Ryzen 9 9950X3D, 3
  corridas cada uno): Estéreo **95,9** fps en el visor (p95 del par 11,2 ms); Multiview **130,7** (p95 del par 8,2 ms),
  **+36%**; Multiview sin el campo unión (`SAVAGEWIND_VR_SYNC_UNION_FOV=0`) 136,0: el borde completo del ojo derecho
  cuesta ~4%. GPU al 59-65%: el límite es la CPU (el hilo del juego) en los dos.
- **Multiview aplica Video > Color y Nitidez** (por ojo: la capa de cada ojo graduada / afilada y escrita de vuelta,
  como hace el TAA por ojo; `exec.c mv_scene_post`). Automático ya no vuelve a Estéreo por ellos; sólo el Escalado (que
  cambia el tamaño de la escena) sigue haciéndolo. Kakariko con saturación 150, contraste 120, temperatura 30 y
  nitidez 60: ojos 0,05-0,21% distintos, la imagen graduada como en Estéreo. (Un primer intento mandaba 8 bytes de los
  64 del grading - el `sizeof` de un puntero - y los ojos salían grises.)
- `SAVAGEWIND_TEST_SETTINGS="clave = valor;..."` (tests): más líneas de configuración para las corridas de
  `test_vr_game.py`.

- `test_vr_eye_match.py --mode sync` (nuevo); el test ya no apaga multiview (por un rato ponía `SAVAGEWIND_VR_SYNC=0`
  en todos los modos).

## 2026-11-25 (multiview en un Quest 3: el ojo derecho que recibía la imagen del izquierdo; el HUD estirado)

- **El reporte del jugador** (Quest 3 por Virtual Desktop, el acceso directo Testing, Multiview): el ojo izquierdo
  "todo lleno de movimiento", los ojos distintos, la interfaz alargada. Medido en el visor real con el nuevo
  `SAVAGEWIND_VR_DUMP_EYES=<s>` (cada tantos segundos, las imágenes que recibe cada ojo en 4 presents seguidos, como
  `vrdump_<ráfaga>_<k>_eye<e>.png` en la carpeta de trabajo) y contadores por cuadro en su línea del log (bloques de
  cámara parcheados para el ojo derecho, sin parchear, reusados). Tres fallas, una detrás de otra:
  1. **La cola de presents desfasada**: los dos presents de un cuadro (izquierdo, derecho) se tomaban de la cola como
     dos entradas independientes; una entrada descartada corría todos los cuadros siguientes una entrada (el orden
     visto: L L R L) y los ojos intercambiaban poses. Ahora el ojo derecho se toma sólo como segundo present de su
     propio par (`vr_present_take_right`), los huérfanos se descartan y una cola llena descarta pares enteros.
  2. **Varias poses de cabeza en un mismo cuadro**: el juego arma sus cámaras más de una vez por cuadro y cada una se
     ubicaba de nuevo; la cámara del ojo derecho, hecha con una de ellas, no coincidía con las constantes del cuadro.
     Ahora todas las cámaras de un cuadro usan la primera ubicación del cuadro (vr_camera.c).
  3. **Un tercio de los cuadros dibujados con una cámara que ningún registro tenía** (pares con el ojo derecho igual al
     izquierdo: 0,0% de diferencia, y 0 de ~485 bloques de cámara parcheados en esos cuadros; fake_xr nunca lo muestra,
     ni con `FAKE_XR_HEAD_SPIN`). Los registros ahora son uno por cámara que arma el juego (64 guardados, se buscan los
     últimos 8 cuadros), y un bloque de cámara que no coincide con ninguno toma el ojo derecho de **su propia vista**
     (tres filas de una rotación seguidas de la vista-proyección): la separación entre ojos V_R V_L^-1 es la misma para
     cualquier cámara. Comprobado en fake_xr forzando ese camino en todos los bloques
     (`SAVAGEWIND_VR_SYNC_ADOPT_TEST=1`): la misma imagen que el camino por coincidencia. `SAVAGEWIND_VR_SYNC_DIAG=1`
     anota un cuadro sin su cámara con su bloque y los registros.
  Antes: L vs R por par 0,0% en ~1/3 de los pares. Tras 1+2: sigue ~1/3. Tras 3: falta confirmarlo en el visor (se
  durmió durante la prueba).
- **El HUD estirado en vertical en multiview: encontrado y arreglado.** Los dibujos del HUD leían el cbuf 3 por el
  camino por ojo (las dos copias del bloque de multiview), que salía antes de que corriera `hud_patch` - el diseño
  16:9 sobre la imagen más alta del ojo -. El bloque por ojo ahora se arma con los bytes de hud_patch. fake_xr: la capa
  del HUD en multiview ahora es igual que en Estéreo (corazones, objetos, minimapa, reloj en su lugar).
- El HUD del ojo derecho en el camino sin HUD compara la capa del propio ojo derecho (vk_image_view ya elige la capa
  del ojo en una imagen por ojo).
- **Multiview / estéreo sincrónico: el borde exterior del ojo derecho completo.** Un cuadro del juego dibuja los dos
  ojos y el juego descarta objetos con su cámara, la del ojo izquierdo: el borde derecho del ojo derecho, fuera del
  campo del izquierdo, tenía huecos (árboles cortados, cielo a través de la roca). Ahora los dos ojos usan el campo de
  ambos juntos (el borde izquierdo del ojo izquierdo, el derecho del derecho), enviado tal como se dibujó: el juego
  descarta para los dos (`SAVAGEWIND_VR_SYNC_UNION_FOV=0`: cada ojo el suyo). Quest 3: imagen ~16% más ancha con los
  mismos píxeles. `test_vr_eye_match.py --mode multiview` (Kakariko, humo de chimeneas, ojos a 0 m): 0,00-0,18%
  distinto, PASS; `test_vr_stereo_sync.py --mode multiview` PASS.
- **Tercera persona en Estéreo (el tester de 25.7: "el giro de cámara está roto como antes")**: el cuadro izquierdo
  de un par mantenía el rumbo del cuadro anterior (para las manos, en primera persona) pero tomaba el lugar nuevo de la
  cámara del juego; en tercera persona la cámara del juego orbita a Link, así que la vista miraba desviada un cuadro de
  la órbita respecto de su propio lugar (fake_xr: 1-2 grados por par, más al ritmo de juego de un visor). El rumbo se
  mantiene sólo en primera persona; el cuadro izquierdo ahora coincide exacto con la cámara del juego (0,00 grados).
  Nuevos `test_vr_stereo_sync.py --view third` (PASS) y `--walk` (caminando de costado con las manos al frente, las
  manos comparadas entre ojos: dx 0 en Estéreo, primera y tercera persona).
- No reproducido en fake_xr, consultado al tester: "manos y Link en tercera persona fuera de escala" (la vista está en
  la cámara del juego, Link en proporción; los marcadores de los controles están en la habitación, a propósito) y "las
  manos no acompañan al moverse" (caminando de costado las manos están en el mismo lugar en los dos ojos; girando, 0
  cuadros de diferencia).

- Nuevo `tools/vr/bench/eye_flicker.py` (cambio por ojo entre cuadros seguidos, el corrimiento del parche lejano) y
  `common.launch(ini=...)` / `--ini` (líneas de configuración para una corrida del banco).

## 2026-11-25 (la ventana del monitor oculta en VR; la configuración abierta en Windows; la partida 100%)

- **La ventana del juego en el monitor se oculta mientras corre la sesión del visor** (Opciones > VR > Ocultar la
  ventana en el monitor, encendido por defecto; vuelve si el visor se desconecta). En juego no se actualiza y el
  jugador no quería ver una imagen vieja en el escritorio. Probado con la ventana visible: se oculta al empezar la
  sesión y el visor sigue a 90 fps.
- **Savage Wind Configuration abierta mientras se juega** (lo que el jugador llama "el launcher de Linux") también
  funciona en Windows: PLAY deja la configuración abierta (`--config`) y el juego corre en su propio proceso (`--play`),
  probado con fake_xr. Es `launcher_stays_open`, encendido por defecto.

## 2026-11-25 (multiview: el terreno vuelve)

- Probando con el acceso "BOTW VR Testing" (Multiview), el jugador vio los objetos "más abajo de donde deberían" en los
  dos ojos. En fake_xr: **en multiview faltaba buena parte del terreno** y se veía lo de abajo (el agua, la base de los
  árboles; los objetos parecían flotar o hundirse). Causa: el cbuf 3 se duplicaba por ojo cortado a 512 filas, y las
  lecturas con índice dinámico (los parches del terreno) pasaban de ahí a la copia del otro ojo. Ahora el bloque va
  entero como lo tiene el juego y después sólo las 64 filas de cámara del ojo derecho (`MV_CAM_ROWS`): las lecturas fijas
  de esas filas eligen ojo, todo lo demás lee los datos reales. Multiview sigue 1,42 veces Estéreo en fake_xr.
- Queda en multiview: brillo distinto entre ojos arriba (cielo, niebla lejana), el HUD estirado y la nieve en otro lugar
  en cada ojo (reportada; sin reproducir todavía).
- El lanzador guarda el log de las builds de desarrollo (`savagewind.log`; Testing: `savagewind-testing.log`).

## 2026-11-25 (Funciones experimentales: lo nuevo no degrada el juego por defecto)

- **Opciones > VR > Funciones experimentales** (apagado por defecto; `SAVAGEWIND_VR_EXPERIMENTAL=1`): lo experimental
  sólo corre con esto encendido. Hoy es Renderizado: Multiview; con esto apagado, Multiview elegido juega en Estéreo, la
  fila dice "(requiere Funciones experimentales)" y el log lo explica. Medido en fake_xr: Multiview sin el interruptor
  91 fps (Estéreo), con el interruptor 141 (Multiview). Las pruebas de multiview lo encienden. 13 idiomas.
- La regla queda escrita en [TESTING.es.md](TESTING.es.md): un cambio que puede empeorar el juego entra apagado, y cada
  release dice qué probar y qué ignorar (lo que el tester pidió y respondimos en Reddit).

## 2026-11-25 (el reporte del tester sobre 25,4; reiniciar al cambiar Renderizado; qué probar en cada build)

- amzukk1 sobre 25,4: con cualquier opción de Renderizado los ojos se desconectan al girar, y las manos y Link se ven mal.
  En fake_xr, Estéreo de 25,4 y del build actual es igual a 25,2 (`test_vr_stereo_sync.py --compare`: el mismo giro
  suave, 140 de 140 pares con el ancla del ojo izquierdo, las mismas diferencias entre ojos), y el código de Estéreo no
  cambió fuera de métricas. Lo más probable: Renderizado se aplica sólo al reiniciar, y quien venía de Multiview (el modo
  por defecto de 25,3) y lo cambió jugando siguió en Multiview. La fila ahora dice "(al reiniciar)" mientras lo elegido
  no es lo que corre; el log dice `[vr] rendering: ...`. Pendiente: confirmarlo con su log.
- Las notas de cada release y la guía de pruebas ([CHECKLIST.es.md](CHECKLIST.es.md)) dicen qué probar y qué ignorar
  en ese build (pedido del tester).

## 2026-11-25 (destellos del sol en VR, de verdad apagados; multiview: el HUD estirado)

- **Prueba nueva `test_vr_eye_match.py`**: los ojos a 0 m (FAKE_XR_IPD=0), así que las dos imágenes tienen que ser
  iguales; marca en rojo lo que difiere. En Kakariko al mediodía mostró **discos de destello del sol en los dos modos,
  en otro lugar en cada ojo** (y apagados): el juego también mide el sol en la GPU (fs `6b2c95ce38cf7523` en un destino
  de 1x1, desde la profundidad), así que contestar 0 a sus occlusion queries no lo ocultaba entero. Su resplandor
  (`9fa83457c0203b51`), los nueve discos (`a4484bb9bf4aa4e1`) y la raya (`8bda266a8639cee3`) ahora no se dibujan con
  el destello apagado (VR por defecto, y Video > Destellos apagado en plano). Con eso, en Estéreo lo que difiere a 180°
  baja de 6,5% a 2,9% (lo que queda: bordes de TAA y el fuego, que titila entre los dos cuadros del par).
- **Multiview**: la capa del HUD sale estirada en vertical (corazones arriba de todo, objetos alargados, el minimapa
  cortado, un nombre de lugar duplicado), y la zona del cielo cerca del sol sale más clara en el ojo derecho. Es lo que
  el jugador vio ("cartelería estirada en Y"); en Estéreo, el modo por defecto, el HUD está bien (la interfaz en su
  banda de 16:9). Multiview sigue siendo opcional y experimental.
- macOS: `vk_mv_on` también sin VR (el enlazado fallaba); un build completo sin VR enlaza.
- El acceso directo del escritorio muestra la pantalla de inicio (JUGAR / AJUSTES / ARCHIVOS DEL JUEGO), como el zip
  de Windows y la AppImage; las ramas y el repo de Linux no tienen nada que no esté en main.

## 2026-11-25 (Automático vuelve a Estéreo: multiview todavía difiere entre ojos en el visor)

- **Reporte del visor** (Quest 3, Windows, el build con Multiview por defecto): humo que se ve en un ojo y no en el otro,
  parte del suelo en un solo ojo, el marcador sobre un enemigo corrido entre ojos, y las ventanas de objetos (el cartel
  de un objeto nuevo) estiradas en vertical. Lo primero coincide con lo que multiview todavía no hace: las pasadas que
  mezclan objetivos por ojo y simples se dibujan sólo para el ojo izquierdo (el log `[vk-mv] single target in a
  per-eye pass` lo muestra a cada cuadro), y efectos con objetivos que el juego vuelve a crear no llegan a quedar por
  ojo. **Automático usa Estéreo** hasta que multiview pase un chequeo de igualdad entre ojos en escenas con humo,
  partículas y marcadores (`VR_AUTO_MULTIVIEW` en vr_camera.c); Multiview queda como opción explícita, marcada
  experimental en la UI (13 idiomas). Lo estirado en vertical queda por reproducir (¿también en Estéreo?).
- **macOS** no compilaba: código de multiview (`mv_on`, `vr_sync_camera`) fuera de la guarda `SW_VR`; cualquier build
  sin VR fallaba igual. Arreglado con versiones vacías para esos builds (compila con `make runtime-objs` sin VR).
- El acceso directo del escritorio salteaba la pantalla de inicio (`--play`, copiado del lanzador anterior): ahora la
  muestra, como el zip de Windows y la AppImage de Linux.

## 2026-11-25 (Opciones > VR > Renderizado: Multiview por defecto; Mono; el giro de multiview arreglado)

- **Opciones > VR > Renderizado** (también en la pestaña VR de Savage Wind Configuration; `SAVAGEWIND_VR_RENDER`; se
  aplica al reiniciar): **Automático** (por defecto), Estéreo, Multiview, Mono. Automático elige Multiview si el driver
  lo tiene y no están activos Escalado, Nitidez ni Color de Video (multiview todavía no los aplica); si no, Estéreo.
  Mono nunca se elige solo. El log dice cuál corrió (`[vr] rendering: ...`). Las variables viejas
  (`SAVAGEWIND_VR_STEREO`, `_SYNC`, `_MV`) siguen mandando. Traducido a los 13 idiomas.
- **El giro con el stick en multiview iba a saltos de 0,35 rad (~20°)**: la regla de estéreo "el cuadro izquierdo
  conserva el rumbo con que se colocó el esqueleto, el derecho toma el nuevo" corría en cada cuadro de multiview, donde
  no hay cuadro derecho: la vista quedaba quieta hasta pasar 0,35 rad y saltaba (log de cámara: pasos de 20,4-21,8°;
  en estéreo 1,3-1,5°). Eran también los 18 de ~210 cuadros con la vista contra el ancla de las manos. Ahora: pasos
  de 1,9° como máximo, 0 de 219 cuadros fuera de ancla. `test_vr_stereo_sync.py` comprueba el giro suave.
- **Reporte de amzukk1 sobre 25,2**: los destellos, resueltos; los brazos "no se actualizan bien al moverse". Si jugó
  con las variables de multiview, el giro a saltos de arriba lo explica; si no, queda medir en el visor `vr lead`
  (nuevo: cuánto después del tiempo para el que se ubicaron la vista y las manos se muestra la imagen; fake_xr da 0 ms,
  así que sólo un visor real dice si las manos quedan atrás al moverlas).
- Build final, 3 corridas intercaladas (misma partida y ajustes): **por defecto (Multiview) 144,3 fps en el visor contra
  94,5 en Estéreo**, p95/p99 del par 7,6/8,1 ms contra 11,6/12,2 ms, sin congelamientos. Suite VR completa en el modo
  por defecto: pasa, salvo `window`, `eye_res` y `headtrack` de test_vr_game, que fallan igual con el build anterior
  (78d98a5) y en Estéreo. Automático usa Estéreo si se pide la profundidad de los ojos (`SAVAGEWIND_VR_DEPTH=1`), que
  multiview todavía no adjunta; el comando `camera` lee la cámara del juego en multiview (la de TAA quedaba quieta).
- macOS: el workflow de la imagen de disco acepta un tag vr-* para adjuntarla al release; en Mac no hay runtime
  OpenXR, así que esa imagen juega en plano.

## 2026-11-25 (auditoría de rendimiento: los cuatro modos medidos; ganchos de prueba para lo pendiente)

- Windows, Ryzen 9 9950X3D (~5,3 GHz efectivos), RTX 3090 driver 616.92, fake_xr a 500 Hz, ojos de 1718x1800, build de
  desarrollo de 78d98a5, Meseta de los Albores, partida de scratch, 5 rondas intercaladas:

  | Modo | fps visor (mín-máx) | × | ms por par (p95 / p99) | GPU |
  |---|---|---|---|---|
  | Estéreo normal | 98,5 (97,3-101,5) | 1,00 | 10,15 (10,4 / 11,0) | 69% |
  | Multiview (`SAVAGEWIND_VR_SYNC=1 SAVAGEWIND_VR_MV=1`) | 147,5 (146,0-148,9) | 1,50 | 6,78 (7,3 / 8,0) | 69% |
  | Mono (`SAVAGEWIND_VR_STEREO=0`) | 178,6 (177,0-185,5) | 1,81 | 5,60 (5,8 / 6,1) | 64% |
  | Estéreo sincrónico solo (`SAVAGEWIND_VR_SYNC=1`) | 79,6 (75,5-81,7) | 0,81 | 12,56 | 41% |

  (p95/p99 del par: una corrida con `vr frametimes`.) El hilo traductor ocupado en todos los modos (`nvn-queue` espera
  1-2%): sigue limitado por la CPU. Multiview suma a mono `vr_sync_camera_rows` 6,5%, `sync_draw_dep` 2,2% y
  `vr_sync_patch` 1,4%. No medido: builds PGO, un visor real, una CPU más lenta.
- **Los números que se comparan entre modos**: el `frame` de `state` cuenta presents, no actualizaciones del juego
  (multiview presenta dos veces por par), así que el p95 de antes era bimodal ahí. Nuevo: `vr frametimes [s]` (los
  tiempos entre cuadros del visor: un par cada uno) y en bench.py el p95/p99 del par, las actualizaciones de cámara por
  par (6 en estéreo normal, 3 con multiview o mono: una actualización del juego por par, medida), el modo, la CPU y el
  driver en cada resultado.
- **Fallback**: pedir multiview con un driver sin multiview dejaba prendido el prototipo de estéreo sincrónico; ahora
  VR vuelve al estéreo normal (`SAVAGEWIND_VK_NO_MULTIVIEW=1` corre como con ese driver: estéreo normal, 100 fps).
- **Mono** (`test_vr_mono.py --control`): los dos ojos con los mismos bytes, las dos vistas enviadas desde el centro de
  la cabeza (0,0 mm; estéreo normal ±31,5 mm), un campo simétrico (±0,942 rad: ~20% menos píxeles por grado que un
  ojo), una actualización del juego por par. La comodidad y su uso para personas con visión monocular requieren una
  persona con el visor.
- **Multiview todavía falla** `test_vr_stereo_sync.py --mode multiview`: girando con el stick, 18 de ~210 cuadros tienen
  la vista girada contra el ancla de las manos (estéreo normal: 0 de 288); tres corridas iguales. `test_vr_taa.py
  --mode multiview` pasa.
- **Destellos del sol** (reporte de un jugador sobre el build 2026-11-25.1: se ven aunque estén apagados, planos):
  `test_vr_sun_flare.py` muestra en VR por defecto (destellos apagados) un velo lechoso sobre la imagen y una raya roja
  diagonal fina que también está con `SAVAGEWIND_VR_LENS_FLARE=1`; todavía no se sabe qué los dibuja (lo siguiente: los
  shaders de esas pasadas con `SAVAGEWIND_LOG_NEW_FS`). Es una pista, no una reproducción del reporte.

## 2026-11-25 (reporte externo sobre el build «25, 1»)

- [amzukk1, r/PCVR](https://www.reddit.com/r/PCVR/comments/1y1iyqy/comment/19t68hb27qhmr/) reporta que en el build
  «25, 1» ya no ve los problemas de sombras ni de separación de ojos. Todavía ve destellos del sol aunque los
  desactiva; aparecen planos, sin profundidad. La escala de las manos es correcta estando quieto, pero al moverlas el
  render de las manos se desincroniza de la vista principal.
- Es un reporte de una persona, todavía sin reproducir acá. Próxima comprobación: confirmar el arreglo de sombras y
  separación; verificar el toggle y la profundidad de los destellos; comparar poses quietas y en movimiento para
  encontrar dónde se desincroniza la imagen de las manos. Relacionado con el seguimiento de brazos de B01, sin asumir
  que sea la misma causa.

## 2026-11-25 (multiview: TAA por ojo; 145 fps contra 100 con TAA)

- TAA corre para cada ojo en la composición de la escena: la cámara de cada ojo (vistas y proyecciones de este cuadro y
  el anterior, de vr_sync.c: los bloques de cámara de mvec.c salen de los dibujos con vectores por objeto, que las
  pasadas multiview no hacen y por eso decía "no camera vectors"), la historia de cada ojo, y la salida copiada sobre la
  capa de ese ojo en la escena, que después lee la composición multiview. Por ahora sólo el movimiento de la cámara.
- Configuración por defecto (TAA, HUD aparte), 3 corridas intercaladas: **144,9 fps en el visor contra 100,2 en estéreo
  normal (+45%)**. Todos los cuadros con el TAA de los dos ojos; manos a 0,56 m y el parche lejano a 4 px; girando con
  el stick, las manos a 0,54-0,56 m y sin estelas a la vista.
- Falta: vectores por objeto en las pasadas multiview, los efectos del runtime (CAS, gradación, FSR) por ojo, las
  pasadas del cubemap de 64x64, y un visor real.

## 2026-11-25 (multiview: el HUD aparte por ojo; 150 fps con la configuración por defecto)

- El HUD aparte ya funciona en multiview: la copia sin HUD se hace de cada capa y se conserva para el present del ojo
  derecho. La composición de la escena no se detectaba porque la escena tiene dos capas (`scene_composite` pedía una);
  ahora la acepta sólo para esa copia, y los efectos del runtime (CAS, gradación, FSR) siguen salteándola para que los
  ojos no difieran. Multiview pone SMAA en lugar de TAA por su cuenta (TAA todavía no es por capa).
- Con la configuración por defecto (HUD aparte): **150,5 fps en el visor contra 98,3 en estéreo normal con TAA y 104,3
  con SMAA**; las manos a 0,56 m, el parche lejano a 4 px; los dos ojos sin HUD y la capa del HUD con 2,2% opaco, como
  en estéreo normal.
- Falta: TAA por capa, las pasadas del cubemap de 64x64 que todavía mezclan, y probarlo en un visor.

## 2026-11-25 (multiview funcionando: 150 fps en el visor contra 104)

Con `SAVAGEWIND_VR_SYNC=1 SAVAGEWIND_VR_MV=1` (y por ahora `SAVAGEWIND_VR_TAA=0 SAVAGEWIND_VR_HUD=0`) los dos ojos se
dibujan en una pasada desde un cuadro del juego. fake_xr, ojos de 1718x1800, RTX 3090, builds de desarrollo, 3 corridas
intercaladas: **150 fps en el visor contra 104 en estéreo normal (+45%)**, GPU 64%. Las manos a 0,56 m y el parche
lejano a 4 px en los dos modos; el brillo de cada ojo como en estéreo normal. Cómo funciona y las pruebas:
[PERFORMANCE.es.md](PERFORMANCE.es.md).
- **Los ojos salían iguales** (corrimiento 0, correlación 1,00): la variante multiview declaraba los slots por ojo como
  `sampler2DArray`, pero la información del shader seguía diciendo 2D, así que el renderer enlazaba una vista 2D de la
  capa 0 y las dos vistas leían el ojo izquierdo. Encontrado con `SAVAGEWIND_VR_MV_DEBUG`: correr la cámara de la copia
  derecha no cambiaba nada, ponerla en cero dejaba el ojo derecho negro.
- **El ojo derecho salía negro**: texturas de media resolución se dibujaban en pasadas junto a un array 2D (800x450) que
  no se convertía, y esas pasadas mezcladas dibujan sólo el ojo izquierdo. Ahora los arrays también son por ojo (capas
  intercaladas, 2L + e) y los objetivos simples de una pasada mezclada pasan a ser por ojo en el cuadro siguiente.
- **Lo que costaba**: 75 -> 83 fps achicando la copia por ojo del cbuf 3 (64 KB por dibujo con los shaders de índice
  dinámico: ahora hasta 8 KB y reusada por bloque), y 83 -> 150 sacando un `getenv` de depuración que se llamaba en cada
  dibujo (40% del hilo traductor: el getenv de Windows es lento).
- Falta: el HUD aparte y el TAA por capa, las pasadas del cubemap de 64x64 que todavía mezclan, y probarlo en un visor.

## 2026-11-25 (multiview: el techo medido, la variante multiview de los shaders)

- **El techo:** en mono (`SAVAGEWIND_VR_STEREO=0`, una traducción por cuadro del visor) 173 fps en el visor contra 102
  en estéreo, la GPU al 68%. Es lo que vale traducir una vez por par; con multiview la GPU vuelve a dibujar las dos
  vistas (~145 pares por segundo con la GPU de hoy, más con las sombras y los vértices una vez).
- **Shaders:** variante multiview (`ShaderKey.multiview`): el cbuf 3 declarado el doble de largo (ojo izquierdo y
  después derecho) y cada lectura `gl_ViewIndex` x sus filas más adelante. Se eligió así y no con un ternario porque la
  salida del traductor pasa por una reescritura que reconoce los patrones `cbN[i][j]`. `SAVAGEWIND_VK_MV_TEST=1`
  traduce y compila la variante de cada shader que usa el juego: 0 fallas en ~400. Detalle y lo que sigue en
  [PERFORMANCE.es.md](PERFORMANCE.es.md).

## 2026-11-25 (por qué rendimos menos que BetterVR; las herramientas de medición en el repo)

- **Por qué:** en estéreo cada cuadro del visor son dos cuadros enteros del juego, y cada dibujo se traduce a Vulkan dos
  veces en un solo hilo (~4,9 ms de CPU por cuadro del juego, ~3460 dibujos): el techo es ~100 pares por segundo con la
  GPU al 70%. BetterVR 1.0.1 hace un cuadro por cuadro del visor y dibuja cada cosa una vez en dos capas con multiview de
  Vulkan (`gl_ViewIndex` elige la cámara de cada ojo; su Cemu: `HostModule::UseMultiviewStereo`, LatteView.cpp), dibuja
  una vez lo que no depende del ojo, y tiene render foveado. Los fps de BetterVR no se midieron en esta máquina (necesita
  Cemu y el juego de Wii U).
- **El plan:** multiview en nuestro backend (las pasadas de la escena en objetivos de dos capas; los shaders traducidos
  leen la cámara por vista; las demás pasadas una vez). Estimado: ~1,4 veces los fps de hoy, limitado por la GPU. Todo
  en [PERFORMANCE.es.md](PERFORMANCE.es.md).
- **Herramientas en el repo** (`tools/vr/bench/`: `bench.py`, `stereo_depth.py`, `profile.py`; antes sólo en una carpeta
  local) y las reglas de medición (partida propia por corrida, PGO ~10%, variantes llamadas `savagewind.exe`, corridas
  intercaladas, cabeza fuera de los ejes, comparar ojos y no sólo cuadros) en [PERFORMANCE.es.md](PERFORMANCE.es.md).

## 2026-11-25 (estéreo sincrónico: las sombras del ojo derecho)

- El pase de sombras (los tres shaders que leen el mapa de 896x896; `SAVAGEWIND_VR_SYNC_DUMP=896` vuelca sus constantes
  y su GLSL) reconstruye la posición en espacio de vista desde la profundidad, elige la cascada por profundidad (fila
  58 del bloque de contexto) y lee su matriz en la fila 42 + 4 x cascada: tres 4x4 desde el espacio de vista (S V^-1).
  Las filas 21-34 son la cámara del cuadro anterior (vista, vista-proyección, proyección, inversa).
- En la pasada derecha, en un bloque de contexto completo: las cascadas pasan al espacio de vista del ojo derecho
  (M D^-1, D = V_R V_L^-1) y la cámara del cuadro anterior a la del ojo derecho.
- **Resultado** (fake_xr, Plateau, cabeza girada 37°): con SMAA el ojo derecho queda como en estéreo normal, salvo un
  árbol lejano negro; las sombras están bien. Más o menos una captura de cada seis sale con el ojo derecho más oscuro
  (también el cielo): pendiente. Con `SAVAGEWIND_VR_SYNC_MONO=0` vuelven las rayas negras en el pasto: las pasadas
  mono ayudan.

## 2026-11-25 (estéreo sincrónico: pasadas mono)

- **Pasadas mono** (`SAVAGEWIND_VR_SYNC_MONO=0`: todo redibujado): la pasada izquierda marca los objetivos dibujados
  por dibujos cuyo shader lee las filas del constant buffer con la cámara (el traductor ya sabe qué vec4 lee cada
  shader), o que leen una textura marcada antes en el cuadro; las copias pasan la marca. La derecha sólo corre los
  dibujos, borrados y copias hacia objetivos marcados; el compute queda del izquierdo. Regla extra: una textura que
  un dibujo marcado leyó y que después se vuelve a escribir en el cuadro también se marca. El mapa de sombras (896x896)
  se dibuja cascada por cascada, leída cada una en el medio; sin esto la derecha encontraba sólo la última y el ojo
  derecho quedaba todo en sombra (negro salvo el cielo). Con la regla la derecha redibuja ~98% de los dibujos (las
  cascadas incluidas); sin ella ~69%.
- **Resultado** (fake_xr, Plateau): el ojo izquierdo igual que en estéreo normal (brillo medio 162 / 162 / 160:
  estéreo, sync sin pasada derecha, sync), las manos a 0,56 m en los dos modos y el fondo a 4 px, como el estéreo
  normal. Falta en el derecho: las sombras del sol mal en una zona grande con borde recto (el límite de una cascada: la
  matriz del pase de sombras desde el espacio de vista, S * V^-1, es la del ojo izquierdo - el updateShadows.cpp de
  BetterVR), y un árbol lejano negro. Las marcas buscan la cámara de los últimos 16 cuadros: después de una pausa el
  juego va varios cuadros adelante del traductor, y con 4 el ojo derecho salía negro a veces.
- **Velocidad:** visor 85-87 fps contra 102 en estéreo normal. La derecha redibuja casi todo, y se suman las copias de
  las listas (~6% del hilo traductor), la búsqueda de la cámara (~5%) y las marcas (~1,4%). Simular una vez por par no
  ayuda mientras el límite sea el hilo traductor.
- **Lo que sigue:** las matrices de sombra desde el espacio de vista en la pasada derecha; una copia por escritura de
  las texturas que se reescriben (para no redibujar las cascadas, ~30% de los dibujos); los árboles lejanos y el resto
  de la lista de BetterVR.

## 2026-11-25 (estéreo sincrónico: los dos ojos del mismo cuadro del juego, primer prototipo)

Empezado lo de BetterVR 1.0.1 (un cuadro del juego, cada dibujo dos veces). Plan, pasos y detalles en
[SYNC_STEREO.md](SYNC_STEREO.md) (en inglés).
- **Censo** (`SAVAGEWIND_VR_SYNC_CENSUS=1`): dónde está la cámara en los constant buffers. En el cbuf 3 de los shaders
  de la escena: vista +0x00, vista-proyección +0x30, proyección +0x70, inversa +0xb0 (los slots 0-3 de BetterVR); otro
  bloque en cbuf 4/5 (inversa +0x2b0, vista +0x2e0); la posición del ojo en algunos. Sin matrices modelo-vista por
  objeto. La vista hecha desde ojo/punto/arriba como sead::LookAtCamera coincide con la del juego (1e-4).
- **Prototipo** (`SAVAGEWIND_VR_SYNC=1`, apagado por defecto): cada cuadro del juego es el ojo izquierdo con el paso
  entero; en el present, ya copiado el izquierdo al visor, el traductor ejecuta otra vez las listas del cuadro con la
  vista, vista-proyección, proyección, inversa y posición del ojo derecho en los constant buffers, y lo presenta como
  ojo derecho. Las listas se copian al ejecutarlas: sin copia se cayó (el juego regrababa esa memoria durante la
  pasada derecha).
- **Resultado** (fake_xr, Plateau, cabeza girada 37°): el ojo derecho sale del mismo cuadro y corrido como un ojo (la
  iglesia, el árbol, el brazo de Link). Pero la luz está mal en los dos ojos: en el derecho un árbol negro y rayas
  negras en el pasto; el izquierdo perdió las sombras del pasto y se ve lavado. La segunda pasada redibuja pasadas
  que guardan estado entre cuadros o que no dependen del ojo (cascadas de sombra, exposición, historiales), y el
  cuadro izquierdo siguiente lee lo que dejó la derecha. Visor 103 -> 87 fps (el hilo traductor hace todo dos veces,
  más las copias y la búsqueda; GPU 70 -> 57%). El modo normal no cambia (mismas medidas con el build anterior).
- **Lo que sigue:** pasadas mono (en la pasada izquierda marcar qué dibujos leen la cámara o texturas que dependen
  de ella; la derecha repite solo esos), una copia por ojo de lo que guarda estado entre cuadros, y las matrices de
  sombra en espacio de vista, luces, cielo, reflejos y partículas (la lista de BetterVR).

## 2026-11-25 (los dos ojos en el mismo momento del juego: probado, no se puede todavía)

Pedido: que los dos ojos muestren el mismo estado del juego en vez de medio paso aparte. Hoy cada ojo es un cuadro
entero del juego (simulación y dibujo) y el derecho va medio paso después: a 90 pares por segundo, 5,6 ms de juego;
Link corriendo (~5 m/s) se corre ~2,8 cm entre un ojo y el otro. La cámara ya mira desde el mismo lugar en los dos
(el ancla del par, 2026-11-24, y las manos hoy); lo que queda corrido es lo que se mueve en el mundo.
- **El cuadro derecho sin tiempo de juego** (`SAVAGEWIND_VR_PAIR_SECOND=0.000001`, girando con el stick, contra 0,5):
  el mundo lejano se corrió +27 px entre la primera y la segunda captura en vez de -45 (el giro, al revés o trabado), y
  a los ~20 s el juego se cayó dentro de su propio código (lectura en 0xffffffffffffffff, swgame.dll; cuadros guest
  70001c166c 700016d0fc 700012d9f4 ...). Con 0,5, en la misma prueba, nada raro. Lo mismo que el 2026-11-20 con 5% y
  0,001 (cadenas pateadas, Link sin girar): el juego divide por el paso y cuenta cuadros, así que un cuadro sin tiempo
  no equivale a no simular. Descartado.
- **El cuadro derecho sólo dibujando** (saltar la simulación): habría que saltar la parte de cálculo del loop del juego
  (sead GameFrameworkNx) en los cuadros derechos. La función del loop en el código generado (0x7001012830, donde se
  pide el intervalo, 0x700101287c) no llama al cálculo de forma directa ni por dispatch; sin el proyecto Ghidra
  (`tools/ghidra/gq.py`, no está en esta máquina) no se encontró dónde se separan cálculo y dibujo. Además la cámara se
  actualiza en el cálculo (rt_lookat, en doUpdateMatrix): sin cálculo el ojo derecho no tendría su propia vista, y
  habría que rehacer sus matrices (vista, proyección y las cascadas de sombra, que dependen de la cámara) antes del
  dibujo. No probado.
- **Como BetterVR 1.0.1** (`ref/BetterVR`): un solo cuadro del juego, y el emulador (Cemu modificado) repite cada draw
  para los dos ojos cargando las constantes de cámara de cada ojo: ~2200 líneas de hooks del juego
  (`resources/BreathOfTheWild_BetterVR/src`: las subidas de cámara por llamador, sombras, luces, reflejos, partículas,
  entorno) más los cambios al emulador. Acá sería: en el hilo de traducción, cada pase de la escena dos veces con los
  buffers de cámara del ojo derecho, y las direcciones de 1.9.0 en lugar de las de Wii U. Además ahorraría la mitad de
  la simulación (CPU). Es el camino correcto pero son semanas de trabajo; no empezado.
- Queda como está: un cuadro por ojo, medio paso aparte.

## 2026-11-25 (reporte de un tester: las sombras del ojo derecho, las manos cruzadas al girar)

Un tester con vr-2026.11.24.6: el ojo derecho sin las sombras de los árboles y con zonas oscuras planas en su lugar, los
ojos que no fusionan, y las manos y el cuerpo de Link "cruzados".
- **Las sombras una vez por par (O13), apagadas por defecto.** Reproducido en fake_xr en la torre de Hebra: el ojo
  derecho con la cúpula oscura y la sombra de la baranda en otro lugar; con `SAVAGEWIND_VR_SHADOWS_ONCE=0`, los dos ojos
  iguales. El juego ajusta las cascadas a la cámara de cada cuadro, así que el ojo derecho leía los mapas del izquierdo
  con sus propias matrices (los mapas son la misma textura en cada cuadro: no es un doble buffer). Las mediciones de
  antes miraban el parpadeo de un ojo entre cuadros, nunca un ojo contra el otro. Cuesta ~4-6% de fps en el visor
  (build de desarrollo: 110,2 -> 103,2). `SAVAGEWIND_VR_SHADOWS_ONCE=1`: encendido.
- **Las manos al girar (B11).** Midiendo la disparidad de la mano entre los ojos (correlación de parches, el campo de
  cada ojo de fake_xr): quieto, 91 px = 0,54 m (la mano está a ~0,53 m); girando con el stick, 66-72 px = ~0,7 m, y en el
  ojo derecho la mano se corría 28 px (~2 grados) respecto de quieto, en el izquierdo 1 px. El cuadro derecho se dibuja
  con el ancla del izquierdo (el arreglo del 2026-11-24 para el mundo), pero el juego a veces calcula el esqueleto de
  Link después de la cámara del cuadro, cuando `c.anchor` ya es el fresco del cuadro derecho. Ahora
  `vr_tracking_to_world`, en el cuadro derecho de un par, usa el ancla del par (y la posición de Link de esa cámara para
  el arrastre): girando, 90-96 px = 0,51-0,54 m, el infinito en 0 px. Con `SAVAGEWIND_VR_PAIR_ANCHOR=0` las manos
  quedaban bien pero el mundo no (la montaña lejana con 14 px, como a 3,5 m).
- Probado y descartado como causa de las manos: el arrastre del ancla (`SAVAGEWIND_VR_FRESH_ANCHOR=0`) y el cuerpo
  girado a la vista (`SAVAGEWIND_VR_BODY_FOLLOW=0`): igual de mal.
- `test_vr_game.py` arms y cinema pasan; headtrack ("projection layer") y stereo (sin desplazamiento vertical / dx 0)
  fallan igual con la base sin estos cambios. Caminando, Link corre con los brazos de la animación: las manos no se ven
  y no se pudo medir.

## 2026-11-25 (rendimiento en VR, Windows: el HUD una vez por par, menos preguntas por draw)

Medido como la entrada de abajo (fake_xr sin límite, ojos de 1718x1800, RTX 3090, la partida al 100% en la Meseta), con
builds de desarrollo (`make VR=1`, sin el PGO del release: ~10% más lentos que el zip), 4 corridas intercaladas por caso:
**103,8 -> 108,0 fps en el visor (+4%)**, p95 5,65 -> 5,34 ms.
- **El HUD solo en el cuadro del ojo derecho (O16).** En estéreo con el HUD aparte, el ojo izquierdo recibe la copia
  sin HUD de su cuadro (`fg_copy_hudless`) y la capa del HUD sale del cuadro derecho, así que lo que el juego dibuja en
  el cuadro izquierdo sobre la imagen compuesta después de esa copia (el HUD, ~85 draws) no llegaba a ningún lado: ahora
  esos draws vuelven enseguida. 103,8 -> 105,6 (las 4 corridas por encima de las 4 sin el cambio). La capa del HUD
  igual (2,2% de píxeles opacos con y sin), el ojo izquierdo sin HUD como antes. `SAVAGEWIND_VR_HUD_ONCE=0`: como
  antes.
- **Si el HUD va aparte, preguntado una vez por cuadro.** `fg_draw` lo preguntaba dos veces por draw
  (`vr_hud_wanted` -> `vr_world_view`: eventos, diálogos, pausa, mira, menú; ~7000 veces por cuadro); ahora se pregunta
  en el present de antes, en el mismo hilo (un cuadro que entra al juego recibe el HUD en su imagen). Y el draw del
  desenfoque (DOF) compara primero su shader. 105,3 -> 108,0.
- `test_vr_game.py` cinema, hud, menus, stereo, scope y exit pasan; firstperson falla en "agachado" con la partida al
  100% también sin estos cambios (el ojo no baja: 1,55 -> 1,55 m).
- Perfil del hilo que traduce (`prof` + muestras): ~3460 draws por cuadro; sets 1,36 ms, preparación 1,28 ms, texturas
  0,71 ms, vértices 0,52 ms por cuadro. Probado y descartado: saltear en un ojo el pase 64x64 (una cara del cubemap de
  reflejos, que el juego rota por cuadro: otra cara quedaría con la imagen vieja); juntar las publicaciones de la cola de
  mvec (`mvec_resolve` espera por ellas).

## 2026-11-25 (Windows: medición del rendimiento de VR)

Los zips de Windows de vr-2026.11.24.3 y vr-2026.11.24.6 contra fake_xr (`FAKE_XR_HZ=500`, sin límite;
`SAVAGEWIND_VR_STEREO=1`; ojos de 1718x1800; RTX 3090, driver 616.92; la partida al 100% en la Meseta de los Albores;
~12 s en el mundo y 15 s medidos). Fps del visor = delta de `frames` del comando `vr`; fps del juego = delta de `frame`
de `state`; p95 de `frames 10`; GPU = uso medio de la 3090 del visor (nvidia-smi); congelamientos = líneas
`acquire blocked` / `present took` / `a wait for a submission took` de más de 100 ms. Mediana de 3 corridas (5 para .6 y
sus variables, intercaladas: la primera tanda de .6 salió con ruido, 98-109 fps con la GPU al 56-70%).

| Caso | Fps del visor | Fps del juego | p95 | GPU % | Congelamientos |
|---|---|---|---|---|---|
| .3, ventana oculta | 88,4 | 176,8 | 6,57 ms | 79 | 0 |
| .6, ventana oculta | **113,8** | 227,5 | 5,16 ms | 77 | 0 |
| .6 + `SAVAGEWIND_VR_SHADOWS_ONCE=0` | 109,5 | 219,0 | 5,19 ms | 75 | 0 |
| .6 + `SAVAGEWIND_VK_BARRIER_FRAG=0` | 112,6 | 225,2 | 5,18 ms | 77 | 0 |
| .6 + `SAVAGEWIND_VR_MIRROR=1` | 113,6 | 227,3 | 5,15 ms | 75 | 0 |
| .3, ventana visible | 95,7 | 191,4 | 6,12 ms | 78 | 0 |
| .6, ventana visible | 112,6 | 225,4 | 5,19 ms | 76 | 0 |

- **.3 -> .6: 88 -> 114 fps en el visor (+29%) con la ventana oculta, 96 -> 113 (+18%) con la ventana visible.** Casi
  lo mismo que en Linux, aunque Windows ya tenía la geometría en memoria de video.
- Con .3 la ventana oculta iba más lenta que la visible (88 contra 96): presentar una ventana oculta costaba más.
- Cada variable contra ninguna: **la sombra una vez por par** (O13) vale ~4% (109,5 contra 113,8; las 5 corridas por
  debajo de la base). **Las barreras solo de fragmentos** (O12) ~1% (dentro del ruido). **El espejo** (O11): igual
  con o sin él; en Windows `vkQueuePresentKHR` no retenía los envíos.
- Ningún congelamiento de más de 100 ms en ninguna corrida.
- El log de .6 dice `[vk] presents on a thread of their own (second queue)`: RTX 3090 (Ampere), la del bus PCI 3,
  elegida por ser la del visor (la PC tiene dos 3090 y una GPU integrada de AMD).
- **Calidad (O13):** `shot <tag> 2` en la torre de Hebra (`-3613.75,447,-981.16`, cabeza quieta), 3 pares, % de píxeles
  que cambian más de 8 niveles entre cuadros seguidos: por defecto ojo izquierdo 0,55 / 3,71 / 3,53%, derecho
  0,87 / 0,97 / 1,06%; con `SAVAGEWIND_VR_SHADOWS_ONCE=0` izquierdo 0,62 / 3,38 / 0,44%, derecho 0,50 / 0,80 / 0,75%.
  El salto del ojo izquierdo a ~3,5% pasa en los dos casos, y las diferencias son bordes (el jitter del TAA tras la
  pausa de ~7 s de cada captura), no sombras: sin parpadeo de la sombra una vez por par. Distinto de Linux
  (0,07-0,16%): en Windows cada captura frena el juego varios segundos.
- Cómo: `tools/vr/test_vr_game.py` launch/ready con un lanzador propio (para mostrar la ventana), el exe de cada zip con
  el gamelib ya compilado (mismo GAMELIB_ID `06c1b9eab56ac856` en los dos), una copia de la partida por corrida.

## 2026-11-24 (rendimiento en VR: la ventana espejo, el HUD, las barreras entre pases)

Medido en fake_xr con la configuración del jugador (ojos de 1718x1800, RTX 3090, la partida al 100% en la Meseta de
los Albores, visor sin límite), mediana de 3 corridas: **85 -> ~111 fps en el visor** (171 -> ~222 cuadros del juego
por segundo, uno por ojo), y sin congelamientos de 2 s. Dónde se iba un cuadro: la GPU ocupada 94-96% (5,7 ms por ojo), el
hilo que traduce al 100%; con ojos de 1080 líneas solo 14% más rápido, así que casi todo el costo es por cuadro, no
por píxel.
- **La ventana espejo frenaba el visor.** En X11 `vkQueuePresentKHR` de la ventana a veces tardaba 2 s (más seguido al
  mover la ventana, o cuando no se ve), y los envíos del juego, los del visor incluidos, esperaban detrás: congelamientos
  de 2 s en la mitad de las corridas. Jugando, el visor toma la imagen del propio juego, así que la ventana ya no se
  adquiere ni se presenta (`vr_headless_frame`); sigue en la pantalla virtual (menús, carga), donde el visor la necesita.
  `SAVAGEWIND_VR_MIRROR=1` la muestra jugando (como mucho al ritmo del monitor, sin los cuadros del ojo izquierdo). Se
  probó presentarla desde una segunda cola y un hilo propio (VR los usa ahora, salvo NVIDIA anterior a Turing): el
  driver igual retenía los envíos. Con la ventana visible: 83,2 -> 86,7 fps con el espejo limitado, sin congelamientos
  sin él.
- **El HUD solo** (`vr_hud.frag`) se hace únicamente con el cuadro del ojo derecho, que arma el cuadro del visor: 86,7
  -> 88,8.
- **Barreras entre pases:** cada pase empezaba detrás de una barrera de todos los comandos, así que ningún pase se
  solapaba con el anterior (~200 pases por cuadro, casi todos chicos). En VR ahora solo retienen el trabajo de
  fragmentos, como en Mali: un pase cuyos draws leen texturas o memoria en la etapa de vértices, y cualquier pase después
  de copias, dispatches o escrituras a memoria, sigue esperando todo (`SAVAGEWIND_VK_BARRIER_FRAG=0`: como antes). 88,8
  -> 96; capturas de la Meseta y de Hebra iguales.
- **La geometría del heap del juego en memoria de video en Linux.** El backend de Vulkan mantiene una copia de las
  páginas de geometría del heap en memoria de video y actualiza las que la CPU escribió (su "heap copy"), pero solo
  Windows decía qué páginas se escribieron (MEM_WRITE_WATCH), así que en Linux cada lectura de vértices e índices iba a
  la memoria del sistema por PCIe. Linux 6.7+ ahora las sigue como el GetWriteWatch de Wine: la ventana del invitado
  registrada en un userfaultfd con protección de escritura asíncrona, y PAGEMAP_SCAN lista las páginas escritas y las
  vuelve a proteger (`runtime/platform/linux.c`; `SAVAGEWIND_NO_WRITE_WATCH=1` o un kernel anterior: como antes). La
  copia se enciende sola en GPUs dedicadas: **95,6 -> 107 fps en el visor** (GPU ocupada 96% -> ~80%); en plano a
  1280x720 los mismos 225 fps. Capturas de la Meseta y de Hebra iguales; `test_vr_game.py` cinema, hud, stereo, menus,
  firstperson, scope, weapon y exit pasan.
- **Las sombras del sol del ojo derecho tomadas del cuadro izquierdo, ahora por defecto.** Estaba apagado porque
  parpadeaba. La causa: el ojo del cuadro se pedía a `vr_frame_eye` (el cuadro que el juego está preparando), mientras
  los pases se traducen un cuadro atrás, así que se omitían las sombras del ojo izquierdo, en todos los cuadros
  (comprobado: 42 de 42 cuadros mal en los pases de sombra, 0 con el nuevo `vr_translating_eye`, el cuadro más viejo
  marcado para presentar y todavía no tomado; si aún no está marcado, el ojo que sigue al último presentado). El
  historial de TAA por ojo usaba el mismo ojo equivocado (de forma constante, así que sin daño; ahora 2949 de 2949
  bien). Parpadeo medido con `shot <tag> <n>` (un comando nuevo de fake_xr para cuadros seguidos) en Hebra: 0,07-0,16% de
  píxeles que cambian entre cuadros con o sin. Los draws a un pase que no se empezó ahora vuelven enseguida. 108 ->
  ~111-114 fps. `SAVAGEWIND_VR_SHADOWS_ONCE=0`: los dibujan los dos cuadros.
- Probado y descartado: sin barreras para las lecturas del G-buffer del terreno (+2%).
- `test_vr_game.py stereo` ahora arranca mirando al sol (la vista inicial cambió con el arreglo de las cámaras de la
  UI): su comparación de imágenes a veces no encuentra desplazamiento (dx 0) aunque los ojos difieren (capturas
  revisadas); hud y menus pasan. El dispatch caro (16x1x1, 0,3 ms) es un shader largo y lineal con pocos
  hilos: del propio juego.
- Ninguno tiene todavía un test propio; `test_vr_game.py` cinema, hud, stereo, menus, scope y exit pasan.

## 2026-11-24 (las ventanas de objetos y el inventario en VR; el giro del stick en un menú rápido)

- **I04, las ventanas de objetos chicas y corridas** (la caja del objeto del cofre arriba a la izquierda y lejos, la
  descripción del arma en el inventario chiquita dentro de un casillero, los corazones y los nombres de página apretados
  a la derecha): dos causas. El hook de cámara de VR (`vr_camera_lookat`) tomaba las cámaras de los layouts de la UI
  (ui2d: el ojo sobre el eje z mirando al origen, la de PauseMenuInfo en 0, 0, 989; otras en 0, 0, 10 jugando) por la del
  mundo y les ponía la cabeza: el panel raíz de PauseMenuInfo estaba en 1040, -252, -1810 en lugar de 0, 0, -989 (leído
  de la memoria del juego, en plano contra VR). Ahora se dejan como están, y ya no cambian la vista del juego entre las
  actualizaciones de la cámara del mundo. Además, una proyección de la UI hecha jugando con la forma alta de la imagen
  del ojo se dibujaba en un menú 16:9: en VR las proyecciones de la UI quedan 16:9 (`rt_persp`) y hud_patch las ajusta a
  la imagen. Probado en fake_xr: el inventario igual que en plano; `test_vr_game.py` hud y menus pasan. Validado en el
  visor.
- **C35, un menú rápido giraba la vista al cerrarse:** el stick derecho elige en él y la cámara del juego giraba con el
  empuje; el menú está en la pantalla virtual, donde el hook de cámara sale antes, así que todo el giro llegaba en la
  primera actualización después de cerrarlo. Ahora se recuerda que el menú estuvo abierto, y los giros de la cámara no
  pasan a la vista mientras está abierto y 0,5 s después (`[vr] quick menu: the camera's turn of N deg kept from the
  view`). Validado en el visor.
- `test_vr_game.py` firstperson: caminar hacia atrás con la cabeza girada ahora va hacia donde mira la vista real; en
  el lugar de la partida de prueba eso es un borde, y el control de agacharse que sigue falla (Link cayendo, no
  agachado). Agachándose en llano: el ojo 1,55 -> 0,87 m.
- **U07, las bombas lanzadas con el gatillo derecho caían a los pies** (las redondas siempre, las cuadradas a menudo):
  la bomba de la runa en la mano no pasa por la acción de levantar, así que el flag de cargar que mira el lanzamiento
  del gatillo (+ 0xc40 bit 17) quedaba apagado y el gatillo mandaba A, el "Soltar" del juego. Encontrado comparando el
  objeto del jugador con una bomba afuera (fake_xr): + 0xd44 dice "Bomb" mientras sostiene una ("None" si no; con ella
  el bit 13 de + 0xc48). `vr_player_carrying` la cuenta: al soltar el gatillo se lanza (R), redonda y cuadrada, las dos
  salieron de las manos y no quedó nada a sus pies. Vuelve a sin probar en el visor.
- **Después salían para un costado (visor):** el lanzamiento propio del juego va hacia donde mira el cuerpo de Link, y
  en primera persona su cuerpo está girado hacia la vista solo al dibujarlo. Sin un movimiento de la mano (menos de 0,8
  m/s) la parte horizontal del lanzamiento ahora se gira hacia donde mira la vista, con su velocidad y su altura
  (`[vr] throw: the game's, turned to the view`). En fake_xr, con la cabeza girada 90°: las dos bombas giradas -88° hacia
  la vista. Con un movimiento de la mano sigue mandando la velocidad de la mano (I02).

## 2026-11-24 (pruebas en el visor: los controles en la pantalla de inicio, los ojos separados al girar la cabeza)

- **La pantalla de inicio en el visor no tomaba los controles:** los controles VR sólo llegaban al juego. Ahora la
  configuración los lee ella misma (`vr_menu_pad`: su propia sincronización, sin el mapeo ni los gestos del juego, que
  leen el juego y la hacían caer) y los pasa como un gamepad: A activa, B vuelve, cualquier stick mueve, los gatillos
  cambian de pestaña. El backend SDL de ImGui borra la marca de gamepad cada frame si no hay uno de SDL; la
  configuración la vuelve a poner. Probado con fake_xr: A en JUGAR arranca el juego, con la configuración abierta o no.
- **Girando rápido la cabeza, los ojos separados (WiVRn):** cada ojo volvía a ubicar la cabeza un frame después, y el
  tiempo de display pedido podía diferir en un refresco; un runtime que reproyecta el par con una sola pose mostraba los
  ojos girados entre sí. Ahora los dos ojos del par salen del único `xrLocateViews` del frame izquierdo (con su anchor;
  `SAVAGEWIND_VR_PAIR_ANCHOR=0` vuelve atrás las dos cosas). fake_xr tiene `FAKE_XR_HEAD_SPIN=<grados/s>`;
  `test_vr_stereo_sync.py` a 180 grados/s con los ojos a 0 m: antes 8 muestras (unos 2,3 grados) de corrimiento, ahora 0.
- **Girando suave con el stick, ojos cruzados en lo cercano:** los brazos de Link y lo que tiene en la mano se ubican
  antes que la cámara del frame, con el anchor de la cámara anterior; un frame izquierdo con orientación nueva los
  dibujaba corridos un frame de giro respecto de su vista, sólo en ese ojo. Ahora el frame izquierdo del par usa la
  orientación con la que se ubicó el esqueleto; el derecho toma la nueva para el par siguiente. El comando `vr` cuenta
  `anchor_mismatch`: 0 de 249 frames girando, 240 de 250 con `SAVAGEWIND_VR_PAIR_ANCHOR=0`.
- **Ataques cargados (esquema Quest):** mantener el gatillo derecho abría el menú de armas a los 0,6 s, así que no había
  ataque cargado. Con el arma ya en la mano, el gatillo mantiene Y mientras se lo mantenga; con el escudo en la mano, el
  gatillo izquierdo es una guardia sostenida. Los menús por gatillo quedan para las manos vacías; los grips en los
  hombros los abren siempre (como BetterVR). `test_vr_gestures` 168 chequeos.
- **Después, los gatillos de BetterVR por defecto:** los gatillos mantenidos nunca abren un menú, tampoco con las manos
  vacías (el derecho saca el arma y carga); los menús rápidos son sólo los grips en los hombros.
  `SAVAGEWIND_VR_TRIGGER_MENUS=1` vuelve a activar los menús por gatillo con la mano vacía. La guía de controles y
  C09 / C11 lo dicen, en todos los idiomas. 171 chequeos.
- **Los menús rápidos por los hombros (reporte en el visor):** el grip mantenido en el hombro izquierdo sin arma afuera
  mandaba sólo la d-pad derecha, que el juego toma como el menú de armas (el de arcos necesita el arco tensado: ZR con
  ella); ahora sigue el flujo del menú del grip del arco. El filtro anti-sacudida ya no se come el ZR de un menú
  rápido. Con giro por pasos o sólo cabeza, el eje X del stick derecho se usaba para girar y no se podía elegir en un
  menú rápido; mientras haya uno abierto, el stick va al juego. 173 chequeos.
- **Los FPS en VR:** el overlay contaba frames del juego, un ojo cada uno, y mostraba el doble de lo que muestra el
  visor. En VR cuenta los frames del visor (uno por par estéreo; fake_xr: overlay 100,3 fps, visor 100,0 frames/s), y la
  resolución dinámica apunta a la frecuencia del visor, no a la del monitor. A ya no hace parry mientras la mano
  izquierda levantada cubre (esquema Quest).

## 2026-11-24 (la configuración abierta mientras se juega)

- **Ajustes > Mantener abierto al jugar** (activado por defecto; no en macOS ni en el Modo Juego de Steam): JUGAR suelta
  la sesión VR de la pantalla de inicio, el programa se reinicia como Savage Wind Configuration sola
  (`SAVAGEWIND_LIVE=1`, sin VR; en Linux el mismo proceso, así la AppImage sigue montada) y arranca el juego en un
  proceso propio. Cerrar la configuración deja el juego andando (en Linux espera a que termine, oculta).
- **Ajustes en vivo:** el juego lee `savagewind.ini` cada medio segundo (por su contenido) y aplica cada ajuste de la
  tabla de `set` que cambió, como lo hace `set` (`control_settings.c` control_settings_live): la página VR, render scale,
  upscaler, overlay... No el tamaño de ventana ni pantalla completa, ni lo que necesita reiniciar. La configuración
  también relee el archivo cuando lo escribe el menú del juego. Su página VR muestra las filas del visor mientras el
  juego corre en VR.
- **Una pestaña VR en los Ajustes de Savage Wind Configuration** (entre Video y Audio): las filas de la página VR, para
  ajustarlas antes de jugar o, con la configuración abierta, mientras se juega. Lo que se cambia ahí se guarda al
  instante (hasta cinco veces por segundo) para que el juego lo tome.
- Los sockets de control y de apps acompañantes ya no se heredan (una pantalla de inicio reiniciada retenía el puerto
  del juego).
- **Probado:** `tools/vr/test_vr_live_launcher.py` (fake_xr): JUGAR, la configuración reiniciada como `--config`, el
  juego en su proceso con el visor (343 frames de mundo en 2 s), un cambio en el archivo aplicado en vivo (separación de
  ojos 85, resolución 1080), la configuración sigue abierta al cerrar el juego. Windows compila pero no se probó acá.

## 2026-11-24 (reporte de Steam Frame: un ojo desfasado, ojos cruzados)

- **El reporte:** el ojo derecho seguido se desfasaba del izquierdo, y las cosas cercanas daban efecto de ojos cruzados.
- **La causa:** cada ojo se dibuja en su propio frame del juego, medio paso aparte, y cada frame rehacía el anchor
  (espacio de tracking -> mundo del juego) con la cámara de ese frame: el giro del stick, el giro propio de la cámara,
  el paso de Link y la posición del ojo delante de su cabeza cambiaban entre un ojo y otro, mientras que al visor sólo
  se le informa la pose trackeada de cada ojo. Un giro se veía como un ojo girado respecto del otro; un paso, como una
  profundidad falsa.
- **El arreglo** (`vr_camera.c`): el frame derecho dibuja con el anchor de su frame izquierdo, así los dos ojos miran
  desde el mismo lugar; las manos y el roomscale siguen usando el anchor actual. El compositor ahora recibe la pose
  trackeada de cada ojo, no la movida por la opción Separación de ojos. `SAVAGEWIND_VR_PAIR_ANCHOR=0`: como antes, para
  comparar. Lo que se mueve en el mundo (Link incluido) sigue mostrando su medio paso.
- **Probado** con un test nuevo, `tools/vr/test_vr_stereo_sync.py` (ojos a 0 m, así las dos imágenes deben coincidir):
  con el stick derecho apretado, antes 2-3 muestras (0,6-0,9°) de corrimiento lateral entre ojos, ahora 0; quieto, 0
  en ambos casos. `test_vr_eye_res.py` (1080 / 2160 / 1440 en vivo) y `test_vr_fixed_window.py` (ventana 1280x720)
  pasan; los escenarios stereo, firstperson, hud, snap, motion, walkdir y arms pasan (el conteo de pares estéreo falló
  una vez durante las pausas de captura y pasó al repetirlo). Falta verlo en un visor.

## 2026-11-23 (reporte sobre el rigging de los brazos)

- El jugador reporta que las muñecas siguen los controles, pero los brazos de Link quedan en posiciones poco naturales
  al mover o girar las manos (B01: implementación parcial, falla en el visor). En `vr_skel.c::solve_arm`, la orientación
  de la mano influye en el codo (`SAVAGEWIND_VR_ELBOW_FOLLOW`, por defecto 3) y el objetivo de la muñeca tiene un
  desplazamiento de 5 cm hacia atrás del control. Conviene comparar por separado movimiento y giro en el simulador y en
  el visor antes de cambiar cualquiera de los dos; es un punto probable para investigar, no una causa confirmada.

## 2026-11-23 (rendimiento en vivo en la página VR; escaleras)

- **Resolución de ojos en vivo (V08):** el juego dibuja al tamaño de los ojos sea cual sea la ventana (el arreglo de
  la Steam Frame), así que alcanza con cambiar el tamaño: un cambio en Opciones > VR > Resolución de los ojos (o `set
  vr_eye_resolution`) se aplica desde el cuadro siguiente y las swapchains del visor lo siguen
  (`tools/vr/test_vr_eye_res.py`: 1080, 2160 y de vuelta a 1440 jugando, sin que los ojos se corten). El rango de las
  opciones VR para `set` ahora es 0-4096 (cortaba en 400).
- **Rendimiento en la página VR (V18):** el overlay de FPS, Escala de render, Upscaler, su modo (con Dinámico: más
  chico cuando los cuadros van lentos) y Anti-aliasing, las mismas filas de las páginas de Video, junto a la
  resolución de los ojos.
- **Escaleras (C34):** en una (el bit 4 solo de los flags de movimiento) el stick de caminar se gira de la dirección
  de la vista a la de Link, como hook_FixLadder de BetterVR: adelante sube mire donde mire el jugador. El signo se
  probó en llano (`tools/vr/test_vr_ladder.py`, `SAVAGEWIND_VR_LADDER_TEST=1`: adelante camina según Link, cos 1,00,
  no según la vista; sin la variable, hacia la vista, cos 1,00); falta en una escalera real (no hay cerca de la
  partida de prueba).
- **G14** probablemente no haga falta: BetterVR toma la dirección de la guardia de hacia dónde mira Link, y acá Link
  gira hacia la vista al cubrirse. A comprobar en el visor.

## 2026-11-23 (B10: el rig de los brazos)

- **El reporte del jugador:** en el visor las muñecas seguían a las manos pero los brazos tomaban poses antinaturales.
  Una vista de Link desde afuera mientras los brazos se resuelven en primera persona
  (`SAVAGEWIND_VR_DEBUG_CAM="<atrás> <derecha> <arriba>"`) y un test de poses (`tools/vr/test_vr_arm_rig.py`: diez
  poses y la muñeca derecha girada de -150 a 150 grados) midieron el rig anterior: la guía del codo seguía todo el giro
  del control; el codo quedaba siempre bajo el hombro; los hombros nunca se movían; el giro del antebrazo se tomaba
  cuadro a cuadro sin continuidad ni límite.
- **El rig nuevo** (`SAVAGEWIND_VR_ARM_RIG=0`: el anterior, para comparar): la guía del codo como la de BetterVR
  (abajo, afuera y un poco atrás con el cuerpo) más hacia dónde apunta el control - solo su dirección, nunca su giro -
  suavizada en 0,1 s; el codo puede subir sobre el hombro con las manos arriba; las clavículas giran con una mano en
  alto o estirada desde la altura del pecho (hasta 20 grados, nunca con los brazos colgando); el giro del antebrazo es
  continuo de cuadro a cuadro (una pausa lo conserva), se suaviza hasta 110 grados por lado y vuelve al ángulo
  principal pasados 210.
- En el simulador: todas las poses resueltas (0,000 m), el codo dentro de 5 cm en todo el giro de muñeca, pasos del
  giro de antebrazo menores a 40 grados (sin saltos), clavículas a 20 grados y codos sobre los hombros con las manos
  arriba, sin la túnica en la vista. Falta verlo en el visor.

## 2026-11-23 (el alcance de los brazos; marcadores del HUD y la rueda de stamina en el HUD flotante)

- **P11, nadar:** jugado en el Dominio Zora (`tools/vr/play_water.py`, flags 0x410): la imagen del ojo muestra la
  superficie del agua desde justo arriba, siguiendo la cabeza de Link.
- **Para después:** C34 (escaleras: los flags de movimiento 0x10 / 0x12 marcan una escalera, pero el giro del stick
  ahí no se puede verificar sin poner a Link en una escalera en el simulador) y G14 (la guardia apuntada con la mano:
  todavía no se encontró la dirección de guardia del juego).
- **Brazos:** el ojo en primera persona 4 cm más adelante (22 cm) dejó las manos 4 cm más lejos de los hombros de
  Link, más allá de los 12 cm que el IK puede estirar un brazo (`test_vr_game.py` arms: 2,3 cm cortos). Ahora estira
  hasta 16 cm: 0,000 m.
- **H10, marcadores sobre el HUD flotante:** se parchea el mundo-a-pantalla de la interfaz (1.9.0 main+0xe1dde0,
  encontrado desde Projection::cameraPosToScreenPos de sead en main+0xeb44f8, su único llamador en juego) donde carga
  el resultado (sitio `ui_project`, main+0xe1de34). Nombres y marcadores se dibujan para todo el campo del ojo (45 a
  50 grados por lado) pero el panel del HUD cubre unos 30: quedaban corridos hacia el centro (un enemigo a 30 grados
  aparecía a 17). Ahora van donde la línea del ojo cruza el panel, como hook_PlaceScreenMarker de BetterVR (con el HUD
  siguiendo la mirada; `SAVAGEWIND_VR_UI_PROJECT=0` lo apaga). Es un sitio nuevo: el código del juego se vuelve a
  armar en el primer arranque.
- **H11, la rueda de stamina:** su llamador (main+0xe1ad80) pide un punto 1,8 m sobre los pies de Link, dentro de su
  cabeza en primera persona; ahí queda bajo los corazones (layout -482, +170, como BetterVR). Probado contra fake_xr
  por los contadores (`vr`: ui_stamina, ui_projected); la rueda todavía no se vio en una captura (la partida de prueba
  tiene la stamina llena).

## 2026-11-22 (Steam Frame: el mundo pixelado y los ojos que no se fusionan)

- Un tester con Steam Frame standalone (la AppImage aarch64): el mundo se veía pixelado con cualquier resolución de
  ojos o escala de render (los menús nítidos) y los ojos no se fusionaban (fatiga visual). La causa: ahí el sistema
  deja la ventana del juego de su propio tamaño, el juego dibujaba el mundo a ese tamaño y el frustum de la cámara
  tomaba la forma de la ventana en vez de la de la imagen de los ojos. Ahora el juego dibuja al tamaño de los ojos sea
  cual sea la ventana (device.c; la ventana muestra esa imagen escalada para entrar), el frustum toma la forma de la
  imagen de los ojos (xr_vulkan.c) y, con el HUD en la imagen, los ojos reciben una copia de la imagen del juego, no
  la ventana (exec.c).
- Probado contra fake_xr con la ventana fija en 1280x720 (`tools/vr/test_vr_fixed_window.py`): el juego dibuja a
  1374x1440, las swapchains de los ojos son de 1374x1440 y el desplazamiento entre ojos es el mismo que con la ventana
  con forma de visor (dx -3); antes, 1280x720 y el desplazamiento al doble (dx -6). Falta verlo en el Frame.
- La suite después de integrar la rama de la release: pasan 19 de 20 (agacharse, los menús de pausa y `swing_hits`
  fallaron una vez y pasaron al repetirlos). Queda: en `test_vr_game.py` arms, cada muñeca a 2,1-2,3 cm de su control
  contra un límite de 2 cm, igual sin este cambio (antes de la integración pasaba).
- El trabajo sin commitear (Snapdragon GSR, el overlay manteniendo el botón menú, ojos a 1440 líneas) revisado, la
  opción de resolución de ojos arreglada (elegir 1080 guardaba el 1440 por defecto), traducido a los otros 12
  idiomas y commiteado.

## 2026-11-22 (Beta 1 fixes de Savage Wind, BetterVR 1.0.2, el PC Maker; release vr-2026.11.22)

- **Upstream:** integrados los "Beta 1 fixes" de Savage Wind (2ae4b39: bindings de lock-on y menús, el mapa en
  ultrawide, los compute shaders leyendo copias de sus constant buffers, la fila "Magnesis: acercar", reintento de un
  compilado que se cae).
- **BetterVR 1.0.2:** montado y con la mano vacía, el gatillo derecho calma al caballo (L) en vez de sacar el arma
  (I08; `test_vr_gestures` 162 chequeos). Sus otros dos cambios no hacen falta acá: el parpadeo de los objetivos de
  las runas es un parche FPS++ de Cemu, y el pelo y el casco al mirar arriba no pueden aparecer con la cabeza oculta
  por sus huesos. Las banderas de material que encontró (bit 4 de las opciones de vista, el Z pre-pass) son la pista
  para B08, la sombra de la cabeza.
- **El PC Maker** (rama de otro agente, 098e8ba) integrado, con dos arreglos: con SAVAGEWIND_HIDDEN ningún diálogo de
  error deja colgada una prueba (ni el del runtime ni el del lanzador del AppImage), y el lanzador le pasa TERM / INT
  / HUP al juego y conserva la entrada de la terminal (probado con un runtime de prueba).
- **Las opciones de VR en todos los idiomas:** los 119 textos de Opciones > VR y su guía de controles traducidos a los
  otros 12 idiomas del menú; `tools/i18n_check.py` vuelve a pasar (el workflow Tests fallaba en main desde el
  2026-11-21).
- **Release:** `vr-release.yml` también arma el zip de Windows (make dist VR=1 en un runner de Windows) y ejecuta el
  AppImage x86_64 montado con FUSE antes de adjuntarlo. Sus primeros builds encontraron tres bugs de empaquetado,
  todos arreglados: la libjsoncpp del loader de OpenXR nunca se empaquetaba (vr-2026.11.21 necesita la del sistema),
  el AppImage de Steam Frame traía un loader x86-64, y el lanzador nuevo pisaba el fd 3 de keepalive del runtime del
  AppImage (se desmontaba debajo del juego). El AppImage x86_64 publicado pasa los 16 escenarios de VR (82 chequeos).
- **Pruebas (Linux, RTX 3090, fake_xr):** todas las unitarias (VR, Maker, bindings); `test_vr_game.py` 16 escenarios
  sobre la build nueva (dos chequeos que fallaron mientras otro juego usaba la misma GPU pasan solos). El AppImage
  publicado vr-2026.11.21 pasa los mismos 16 escenarios (83 chequeos) con su propia carpeta de datos.

## 2026-11-21 (la tabla como lista de control; docs en inglés y español; el build de Linux)

- [FEATURES.es.md](FEATURES.es.md) / [FEATURES.md](FEATURES.md) se generan desde [features.py](features.py): cada
  feature tiene tres controles, implementada, validada por las pruebas automáticas de la IA y validada por una persona
  con el visor. La columna humana sale solo de lo que reporta el jugador.
- Las docs están en inglés por defecto con la versión en español al lado (`*.es.md`); se pusieron al día las que
  estaban desactualizadas (README, instalación, el plan, la lista de lo que falta de BetterVR, la checklist, ahora una
  guía corta de cómo probar y reportar).
- Linux: `tools/linux/package.sh` arma el AppImage con VR (`make appimage VR=1`): trae el loader de OpenXR y arranca en
  VR. `.github/workflows/vr-release.yml` lo compila en GitHub y lo sube a un release: publicado en vr-2026.11.21
  (`Savage_Wind-VR-0.6.0-alpha.6-x86_64.AppImage`, con `libopenxr_loader.so.1` adentro). No probado en un Linux con
  visor.

## 2026-11-21 (lo que faltaba de BetterVR, primera tanda)

- **Lanzar con la mano:** la acción de lanzar lo levantado de 1.9.0 (su calc_ main+0xc76f54, del vtable 0x1cd4e20
  hallado por los parámetros ThrowSpeedF / ThrowSpeedY / SquatThrowSpeedF) llama a main+0xc76d60 en el cuadro de
  lanzamiento de su animación, que arma la velocidad en sp + 4. El parche `_grab_throw` (0xc76e20) la pasa a
  `vr_grab_throw`: con el gatillo derecho soltado durante un tirón de la mano, va la velocidad de la mano (la más
  rápida de los últimos 0,1 s, en el mundo, hasta 1,2 veces la del juego). "Cargando algo" es el bit 17 de player +
  0xc40, que pone la acción de levantar (main+0xc7721c, animación GrabUp) y saca el lanzamiento. Generado y compilado
  (233 sitios); no probado en juego: el save de prueba no tiene bombas ni nada para levantar cerca (test_vr_throw.py
  queda para un save que lo tenga).
- **Lo levantado en la mano:** en primera persona, el modelo sobre la cabeza va a la mano derecha (achicado a la
  mitad, la lista de BetterVR) o entre las dos manos. Mismo límite de prueba.
- **Cuerda del arco:** los huesos String_L_2 / String_R_2 del arco (hallados: 5 y 9 en Weapon_Bow_001) van a la mano
  que tensa. test_vr_bow pasa; en la captura del ojo la cuerda va del extremo del arco hacia la mano.
- **Sin sacudida de cabeza:** Y o ZR sin arma / arco encima (los modelos que vr_skel ve en las manos y la espalda)
  no llegan al juego. test_vr_bow y gestures pasan (no bloquea cuando hay).
- **Opciones:** Preset de confort (Suave, Equilibrado, Inmersivo), Zona muerta de los sticks (test_vr_input_map: 61),
  Corregir la rotación de los controles (el arreglo de BetterVR para el runtime Oculus), Guía de los lugares del
  cuerpo (con "siempre", fake_xr recibe 5 discos más por cuadro), Trayectoria del arco con modos, página Guía de
  controles. Perfiles de Steam Frame, Pico y Vive Cosmos.
- **Escalando:** el ojo iba adelante de la cabeza de Link, o sea dentro de la roca. Ahora va 0,3 m detrás de su
  cabeza (hacia donde mira su cuerpo). En el simulador, caminando contra la roca grande del Plateau hasta trepar
  (banderas 0x90): `climb_eye` 0,96 y la captura muestra la pared desde afuera.
- test_vr_game gestures: el chequeo esperaba el texto de antes del esquema Quest; arreglado.

## 2026-11-21 (silbido con los gatillos; opciones que no hacen nada en VR)

- **Silbido:** las dos manos a menos de 28 cm de la boca y los dos gatillos apretados (con los grips a 20 cm no te
  salía). Con las dos manos ahí, los gatillos no hacen otra cosa (ni cubrir, ni atacar, ni parry). `test_vr_gestures`
  (144).
- **Opciones:** en VR el visor fija la forma de la ventana, la frecuencia y el campo visual, y la ventana no es lo que
  ves: las filas de Video que no hacen nada ahí (pantalla completa, modo, monitor, tamaño de ventana, rellenar
  pantallas anchas, HUD en los bordes, campo visual, fps, VSync) ya no aparecen con VR. Nueva fila VR > Resolución de
  los ojos (1080, 1440, 1800, 2160; desde el próximo inicio; antes solo `SAVAGEWIND_VR_HEIGHT`): en fake_xr con 1440
  la ventana queda en 1374x1440. `test_vr_options` pasa. Lo que sí cambia la imagen en VR: el preset de gráficos y
  Video avanzado (escala de render, distancia de dibujado, sombras, anti-aliasing...).
- Revisado: arranca con VR y pasa la pantalla de inicio (el arreglo del cierre al iniciar, ahora con VR de verdad).

## 2026-11-20 (parry con el gatillo, menús de arco y flechas, la flecha sobre el arco, eventos lentos)

- **Eventos lentos (la causa):** en tu log, Demo040_0 (aparece el cofre) y Demo000_0 (abrirlo) iban a 38-46 fps
  dibujando lo mismo que el juego normal. El perfilador (prof, swprof) en el despertar de la nueva partida en fake_xr:
  el hilo que traduce los dibujos pasaba 18,8 ms por cuadro en "draw/pre", casi todo dentro de `event_at_30`
  (framepace.c), que VR pregunta en cada dibujo durante un evento (vr_world_view) y que leía una variable de entorno
  con getenv cada vez. Ahora la lee una vez. El despertar: de 88 a 260-318 fps (fake_xr, sin tope). Esto estaba desde
  antes de esta semana: cualquier evento en VR iba lento.
- **Gatillo izquierdo:** cubre y, al apretarlo, hace parry (A 0,06 s después de levantar el escudo). A mientras
  cubrís ya no hace nada (era el parry del juego). B ahora es un toque: mantenido ya no corre (correr: L3).
- **Grip derecho mantenido:** el menú de arcos (el arco tensado, ZR, y 0,3 s después la cruceta a la derecha); antes
  abría el de armas. Con el arco afuera, el grip izquierdo mantenido abre el de flechas (cruceta a la izquierda). Al
  soltar, B con ZR todavía apretado cancela el tiro: no sale flecha.
- **La flecha sobre el arco:** mientras tensás, el modelo de la flecha se gira sobre la línea de la mano de la cuerda
  a la del arco (por donde sale). En fake_xr: sin esto quedaba a 66° de esa línea; con esto, 0°. El tiro sigue
  saliendo por esa línea (test_vr_bow pasa).
- **Ventana del objeto nuevo arriba a la izquierda y deformada:** no se pudo reproducir; hace falta una captura.
- `test_vr_gestures`: 142.

## 2026-11-20 (controles propios para el Quest; la ventana del objeto nuevo; opciones en el juego)

- **Esquema Quest (nuevo predeterminado):** A acepta (A), B cancela / guarda / corre (B), stick derecho arriba salta
  (X), gatillo derecho usa lo de la mano derecha (ataque, arco, runa) y mantenido 0,6 s abre el menú de armas,
  gatillo izquierdo cubre / fija (ZL) y mantenido 0,7 s abre el de escudos, grip derecho lejos del cuerpo saca el
  arco y mantenido abre el de arcos, grip izquierdo agarra, Y la runa, X el mapa, el botón de menú la pausa. Los
  lugares del cuerpo siguen igual. Los íconos de los avisos siguen este esquema (saltar: el stick derecho arriba del
  juego). `SAVAGEWIND_VR_CONTROLS=bettervr` vuelve al de BetterVR. `test_vr_gestures` (133) y `test_vr_bow` pasan.
  Contra: mantener el gatillo izquierdo más de 0,7 s ya no cubre (abre el menú); para cubrir largo, la mano levantada
  con el escudo.
- **Ventana del objeto nuevo:** no se pudo reproducir (la partida de prueba no tiene un objeto nuevo a mano). En tu
  log, al recibir el objeto el juego pasó a la pantalla virtual y el mouse se tomó y soltó varias veces (una pantalla
  de menú). Ahora el log dice por qué un evento va a la pantalla virtual (`[vr] event ... on the virtual screen:`).
  De paso: el despertar de la nueva partida (Demo102_0) iba a la pantalla virtual porque la detección del visor de la
  Pizarra se disparaba con sus shaders; en un evento ya no cuenta. Ese evento en 3D es pesado (11 ms por cuadro desde
  la cabeza de Link, 44 pares por segundo en fake_xr), es la escena.
- **Opciones de calidad en el juego:** ya están: Menú → Sistema → Opciones (Video, Video avanzado, VR).

## 2026-11-20 (en el Quest: correr con L3, eventos a 30 fps, el arco, íconos del Quest)

- **L3 mantenido corre** (B mientras se mantiene, después de 0,25 s; un toque sigue siendo agacharse). El visor de la
  Pizarra pasó a R3 mantenido (antes R3 mantenido abría el mapa, que ya está en X mantenido).
- **Eventos a 45-48 fps:** en tu log, Demo017_0 (y Demo002_0) bajaron de 180 a 48-97 fps. Esos eventos se muestran en la
  pantalla virtual (el juego pone su estado de pausa), y ahí cada cuadro del juego esperaba un cuadro del visor y el
  final del anterior: con la latencia quedaba en la mitad. Ahora, en modo estéreo, la pantalla virtual va como el
  juego: dos cuadros del juego por cuadro del visor (el primero no se manda). Simulador, el despertar de la nueva
  partida (Demo102_0): de 95-100 fps a 175, y el tramo de 45 a 87-110.
- **Arco:** no se pudo reproducir (test_vr_bow pasa). Punto frágil arreglado: si en un cuadro el juego no mostraba el
  arco en la mano izquierda, el gesto lo daba por guardado y el gatillo derecho mandaba atacar (sacaba la espada en
  vez de tensar). Ahora el arco sacado con el gesto sigue siendo el arco mientras no se vea otra cosa en las manos.
  Cada gatillo derecho deja en el log qué hizo y qué mostraba el juego en las manos.
- **Íconos del Quest:** con los controles de VR (Botones en Automático) los avisos del juego se dibujan como los
  botones del Quest que hacen cada cosa (A, B, X, Y, LT, RT, L3, R3, menú); en menús y diálogos, las letras tal cual.
  Dibujados por Savage Wind (nada de Meta). Revisado con el volcado de la hoja de glifos en el simulador.

## 2026-11-20 (en el Quest: Magnesis no sube ni baja y es muy sensible; el parry no sale nunca)

- **Magnesis:** con la muñeca (como BetterVR) iba a los costados con nada y casi no subía ni bajaba. Probé después la
  dirección del brazo desde el pecho: con la mano cerca del cuerpo los ángulos salían igual de desparejos (subir 25 cm
  la mano subía 0,5 m el objeto). Ahora cuenta lo que la mano se movió desde que agarraste (suavizado 0,1 s, medido
  desde los pies de Link para que caminar no lo mueva): 0,6 m de mano a un costado lo giran un radián alrededor de
  Link, y hacia arriba lo suben tanto como está de lejos. Simulador (espada a 7 m, con 0,5 m): mano 25 cm arriba,
  +3,5 m; 30 cm a cada lado, ±35°, y volvió al centro; la muñeca sola, nada; el stick, de 2,5 a 20 m.
  `SAVAGEWIND_VR_TRACE_MAGNESIS=1` escribe en el log lo que se movió la mano y lo que se le dio al juego.
- **Parry:** 18 cm en 0,15 s a 1,6 m/s no salía nunca. Ahora 10 cm en 0,2 s a 1 m/s, hacia adelante o alejándose de
  la cabeza.

## 2026-11-20 (en el Quest: parry muy sensible; Magnesis movido con las manos)

- **Parry:** bastaba que la velocidad de la mano izquierda subiera 35 m/s² entre dos cuadros mientras cubrías, y el
  temblor del seguimiento o un movimiento chico lo lograban. Ahora hace falta empujar el escudo hacia adelante
  (horizontal, hacia donde mirás) 18 cm en 0,15 s, a 1,6 m/s o más, con 0,6 s entre parries.
  `test_vr_gestures`: el temblor, un empuje lento y un barrido de costado no hacen parry; un empujón sí.
- **Magnesis con las manos (como BetterVR):** mientras Magnesis sostiene algo, el objeto cuelga de la mano derecha en
  un palo imaginario: sigue sus movimientos y gira e inclina con hacia dónde apunta. El stick derecho solo lo acerca o
  lo aleja; no gira la vista. Se escribe en la acción de Magnesis del juego (distancia, altura, dirección), hallada
  con fnprof y `mem path` (re-notes). La cámara del juego gira sola para seguir al objeto; eso giraba la vista y, con
  ella, la mano en el mundo, y el objeto se escapaba de costado. Ahora, mientras tanto, cualquier giro de la cámara del
  juego se compensa en la vista. `SAVAGEWIND_VR_MAGNESIS_HAND=0` deja los controles del juego. Simulador (la espada
  a 7 m): inclinar la mano 20° la subió 2,15 m, girarla 25° a cada lado la llevó ±24° y al volver al centro volvió;
  stick arriba 1 s, a 20 m; abajo 1,5 s, a 3 m; stick a la derecha, sin giro; B la soltó.

## 2026-11-20 (en el Quest: ZL gira 180°, Magnesis no agarra, cadenas que se sacuden)

- **ZL gira la vista 180°:** ZL sin objetivo pone la cámara del juego detrás de Link, y Link puede estar mirando para
  cualquier lado (por ejemplo después de caminar hacia atrás). Ahora, en primera persona, un giro propio de la cámara
  del juego más rápido que 90°/s, con el stick derecho quieto, no gira la vista: se compensa en el giro de la vista.
  El juego camina a Link según la vista dibujada, así que el stick sigue yendo hacia donde mirás.
  `SAVAGEWIND_VR_KEEP_VIEW=0` lo apaga; el campo de `vr` `kept_turns` cuenta las compensaciones. Simulador: después
  de caminar hacia atrás, un toque de ZL giraba la vista 179,9° y ahora 3,5-6°.
- **Magnesis:** el juego además traza un rayo propio por el centro de la imagen y usa la mano de Link (la de la
  Pizarra Sheikah). Con el campo descentrado de cada ojo, el centro de la imagen no es donde mirás, y con la mano de
  Link movida al control, apuntando a otro lado, no agarraba nada. Probado en `fake_xr` estéreo: solo la mano o solo la
  cabeza no agarraban; con campo simétrico y los brazos sin mover, la cabeza sola levantó la maza 16 m. Ahora:
  - mientras hay una runa apuntada, cada ojo usa un campo simétrico: el centro de la imagen (la mira del juego) es
    hacia donde mira la cabeza;
  - mientras hay una runa apuntada, los brazos de Link quedan con la animación del juego;
  - las runas se apuntan siempre con la mira (la cabeza); se sacaron el rayo amarillo y la opción "Apuntar las runas
    con la mano".
  `test_vr_rune_aim` reescrito: la selección queda en el centro (0,5 0,5), no sigue a la mano y no se dibuja rayo
  (pasa en estéreo).
- **Cadenas y objetos que se mueven solos:** el par estéreo daba 95% del tiempo de juego al cuadro izquierdo y 5% al
  derecho. Los solvers de física corrigen el error de una restricción como error / paso, así que un paso 19 veces más
  corto que el siguiente patea las cadenas y las pilas de objetos. Ahora el par se reparte parejo (50/50): el juego
  corre como en plano a doble cuadro, igual que los ojos alternados de BetterVR. Cada ojo muestra su propio momento,
  medio paso aparte. `SAVAGEWIND_VR_PAIR_SECOND=<0..0,5>` prueba otra proporción. Simulador: caminar en todas las
  direcciones va bien y quieto no se desliza (0,00 m). Las cadenas en sí no se probaron (no hay cadenas en la partición
  de prueba).

`make VR=1 test-vr` y `test_vr_swing_hits` pasan.

## 2026-11-20 (en el Quest: sombras que parpadean en el ojo izquierdo, ataques y agachadas solos)

Reportado: el ojo izquierdo tiene sombras que aparecen y desaparecen (el derecho bien); Link ataca con movimientos leves
de la mano; se agacha solo con el jugador parado.

- **Sombras:** el atajo que deja al ojo derecho reusar los mapas de sombra del izquierdo (`SAVAGEWIND_VR_SHADOWS_ONCE`)
  hacía parpadear el **izquierdo**. Medido en `fake_xr` en estéreo con la cabeza quieta (60 capturas por ojo, píxeles
  que saltan más de 40 de un cuadro al siguiente): ojo izquierdo 2,8% con el atajo contra 0,4% sin él; el derecho ~1,9%
  en los dos casos. Las pasadas de sombra del juego parecen construir sobre los mapas que dejó el cuadro anterior, y las
  del ojo derecho salteadas ya no lo hacen. Ahora apagado por defecto (`=1` lo prende). `test_vr_shadows` ya no exige
  que el atajo suba los cuadros por segundo (hoy daba -13%).
- **Ataques:** el análisis de tajos y estocadas (el de BetterVR) toma un giro rápido de muñeca por un tajo (7 rad/s, la
  mano casi sin moverse). Para apretar el ataque del juego y prender el sensor del arma ahora además pide que la mano
  recorra de verdad (pico de 2 m/s en un tajo, 1,5 m/s en una estocada; un giro de muñeca no llega a 1) y un arma cuerpo
  a cuerpo en la mano de Link (sin arma, el botón de ataque la desenvaina). Lo descartado se cuenta en `swings_ignored`
  (campo de `vr`). `test_vr_swing_hits`: su tajo era solo un giro sobre el lugar; ahora la mano también recorre 0,5 m.
- **Agacharse:** Link no se agachaba: era la vista. La cámara sigue la altura de la cabeza de Link (para bajar al
  agacharse, nadar o montar), y la animación de ataque baja su cabeza 33 cm por medio segundo: cada ataque falso bajaba
  la vista como si se agachara. Ahora la vista sigue a la cabeza solo con las banderas de movimiento de agachado, nadando
  o a caballo.

Probado con el simulador de manos (estéreo): giros de muñeca y movimientos chicos sin arma o con arma → ignorados, sin
ataque; tajos reales (la mano 0,5-0,8 m en 0,2 s) → atacan; la altura del ojo queda en 1,55 m al atacar (antes bajaba a
1,22) y baja a 0,87 m al agacharse con el clic del stick izquierdo. `make VR=1 test-vr` pasa; `test_vr_game --scenario
motion` y `test_vr_swing_hits` pasan.

## 2026-11-20 (en el Quest: Link no giraba con la vista, el stick atrás avanzaba)

Reporte: al girar (la cabeza o el stick derecho) la vista gira pero Link sigue caminando para el mismo lado; a veces el
stick avanza siempre, aunque se empuje atrás; cajas y rocas se mueven solas.

Reproducido con el simulador de manos **en estéreo** (el modo del visor): con el stick atrás, con la cabeza girada 90°
a cualquier lado o después de girar con el stick derecho, Link corría siempre hacia el mismo lado (~32°, hacia donde
miraba al empezar) y, quieto, se deslizaba unos centímetros. En mono todo andaba (los tests de `test_vr_game.py` corren
en mono: no lo veían). Descartado: el giro del cuerpo hacia la vista (`SAVAGEWIND_VR_BODY_FOLLOW=0`, igual).

Causa: en estéreo cada par de cuadros (uno por ojo) avanza el tiempo del juego una vez: el izquierdo tomaba todo y el
derecho **0,001** de un cuadro de consola. El jugador y la física del juego no aguantan ese paso: Link no giraba hacia
el stick y lo que se apoya derivaba (con un paso así, cualquier cosa que se mueva en ese cuadro tiene velocidades
enormes: es lo más probable detrás de las cajas y rocas que se mueven solas). Arreglo (runtime/vr/vr_pair.h): el
cuadro derecho toma el 5% del tiempo del par y el izquierdo el 95% (probado: con 2% ya anda). Los dos ojos ven casi el
mismo momento (0,5 ms de diferencia a 90 Hz).

Probado en estéreo: atrás camina atrás (180°), la cabeza girada 90° a la izquierda o la derecha camina hacia donde
mira (0,1°), después del stick derecho también, y quieto no se mueve (0,00 m). `test_vr_pair` actualizado.

Sin confirmar: las cajas y rocas que se mueven solas (no hay una cerca del punto de prueba) y los "brazos estirados"
que se ven.

## 2026-11-20 (botones del Quest en los menús)

Reporte: el mapeo de botones confunde. Revisado contra los bindings de BetterVR para Touch (Quest;
`src/utils/controller_bindings.h`). En juego coincide: stick izquierdo mover (clic agacharse / catalejo), derecho girar
(clic inventario / mapa), B saltar, A interactuar / correr, Y izquierdo runa, gatillos usar lo de cada mano, grips
agarrar o los lugares del cuerpo. El error: BetterVR tiene **otro mapeo en los menús** y acá los gestos seguían
activos dentro del inventario, el mapa y los diálogos: B mandaba saltar (X) en vez de volver, el gatillo derecho
atacaba (Y: ordenar), A actuaba recién al soltarla y los grips mandaban A. Además los íconos de los botones salían de
teclado ("Más rápido [espacio]") porque los controles del visor no contaban como control.

Arreglo: en los menús (estado de pausa 2, el mapa, la pantalla de título, un diálogo) los botones van por su letra
como en BetterVR: A elige, B vuelve (y acelera el texto), X sostiene, Y ordena, gatillos L / R (pestañas), clic del
stick derecho cierra el inventario (+) o el mapa (-), sin gestos (`vr_pad_menu_buttons`, runtime/vr/vr_input_map.h).
Un botón apretado al abrir o cerrar un menú no cuenta hasta soltarlo. Usar los controles del visor pone los íconos de
control (letras de Nintendo). Probado con el simulador: clic derecho abre el inventario, gatillo derecho pasa a la
página siguiente, B cierra; clic mantenido abre el mapa y al soltarlo sigue abierto, B lo cierra; ya en juego B salta.
Test unitario `test_vr_input_map` (59 comprobaciones).

En juego, los íconos siguen siendo las letras del juego: su X (saltar) es la B derecha, su B (correr / guardar) la A
derecha mantenida, su Y (atacar) el gatillo derecho. BetterVR tiene lo mismo.

## 2026-11-20 (en el Quest: Link caminaba solo y la vista se separaba)

Reporte jugando en el visor: la vista quedaba dentro de una roca o separada del cuerpo, y Link caminaba solo sin tocar el
stick. Causa: la escala de habitación. Con la cabeza a más de 10 cm del ojo de Link empuja el stick hacia la cabeza y,
mientras tanto, deja el ojo quieto en el mundo moviendo el centro de la habitación con cada paso de Link, en cualquier
dirección. Si Link no podía llegar (una roca adelante) o algo lo movía para otro lado (la corriente, una pendiente), la
distancia no se cerraba nunca: el stick seguía empujando y la vista quedaba lejos de él.

Arreglo (runtime/vr/vr_camera.c): el centro sólo acompaña la parte del paso de Link que va hacia la cabeza, sin pasarse;
y si en 0,7 s no se acerca 2 cm, o la cabeza queda a más de 60 cm, la vista vuelve sobre Link y deja de caminar (log
`[vr] roomscale: Link could not reach the head`). Probado: `test_vr_roomscale.py` pasa (0,52 m caminados por un paso de
50 cm); contra una pared de la meseta, con la cabeza 40 cm adentro, Link no avanza, la vista vuelve a 0,18 m de él (el
ojo adelante de la cabeza) y el stick deja de empujar.

## 2026-11-20 (silbido, menú rápido, arrojar el arma: jugado)

`tools/vr/play_misc.py <partida>` (jugar, no test): en la meseta, con el simulador de manos.

| Qué | Cómo | Resultado |
|---|---|---|
| Silbido | las dos manos ahuecadas en la boca (8 cm adelante y 10 cm abajo de los ojos), los dos grips | el control manda d-pad abajo (`controller_buttons` 0x8000) mientras se mantiene; no hay caballo registrado en la partida, así que no viene ninguno |
| Menú rápido | la mano derecha sobre el hombro derecho, grip mantenido 1,2 s, stick derecho a la derecha, soltar | se abre (estado de pausa 4, la pantalla 0x52 dibuja) en la pantalla virtual con las armas; al soltar se cierra y queda equipada la elegida (porra boko) |
| Arrojar | con el arma afuera, la mano sobre el hombro, gatillo derecho mantenido (Link levanta el arma), un tajo hacia adelante y soltar | el arma sale volando hacia adelante (se ve en el aire), la mano queda vacía y el gesto pasa a "nada en la mano" |

El log de modelos (`SAVAGEWIND_VR_LOG_MODELS`) deja de nombrar el arma arrojada: para medir dónde cae hay que seguir el
actor nuevo, no el modelo del arma en la mano.

## 2026-11-20 (Magnesis, nadar, planeador: jugado)

Todo con el simulador de manos contra fake_xr, en estéreo. Herramientas nuevas (jugar, no tests de pasa/falla):
`tools/vr/play_magnesis.py <partida>` y `tools/vr/play_water.py <partida>`.

| Qué | Dónde | Resultado |
|---|---|---|
| Magnesis con la mano | la meseta: la maza soltada con el gesto de la cadera derecha | elegida en el menú de runas (Y izquierdo mantenido + stick derecho a la izquierda: el imán en el HUD), usada con Y, la mano apuntada a la maza (su posición leída del log de modelos), agarrada con el gatillo derecho y levantada con el grip mantenido y la mano arriba: subió de y 249,9 a 271,0 (21 m en 2 s: la subida es de velocidad, mientras la mano está arriba) |
| Nadar | Zora's Domain (`SAVAGEWIND_TEST_WARP=3315.61,255.64,-501.00`), buscando agua alrededor | **bit de nadar confirmado**: flags `0x410` (1 << 4 \| 1 << 10); 6 m nadados con el stick; el ojo a 0,25 m sobre la posición de Link: la vista a ras del agua, las manos debajo (imagen del visor) |
| Planeador | desde 40 m sobre Zora's Domain, salto y salto otra vez en el aire | la partida lo tiene: flags `0x8002` (en el aire + **planeador, 1 << 15, confirmado**); al aterrizar vuelve a 0x18000001 |
| Mirar abajo | la meseta, cuello simulado | a 40° se ven manos, pies y la correa; a 70° el pecho (lo normal de un cuerpo en VR) |

Observado, sin cambiar: planeando, los brazos de Link siguen a los controles (no sostienen la manija del planeador).

Palmeo al caballo: siete corridas más de `play_horse.py` sin montar (antes 2 de 3). Link llega a subirse (flags
`0x80000000`, el caballo corcovea) pero lo tira aunque se lo calme con L; con la resistencia de la partida (una rueda)
no alcanza para domarlo, y las dos veces que montó fueron caballos que no corcovearon. El palmeo sigue cubierto solo por
el test unitario; `play_horse.py` ahora palmea apenas monta y cuenta las trazas `[vr] riding: L (pet the horse)`.

`handsim.py`: reintenta el reemplazo del archivo en vivo cuando fake_xr lo tiene abierto (Windows lo rechazaba).
Traza nueva con `SAVAGEWIND_VR_TRACE_GESTURES=1`: `[vr] riding: L (pet the horse)` en cada palmeo a caballo.

## 2026-11-20 (a caballo; revisión del trabajo de otra IA)

### Revisión de lo hecho con otra IA (Codex)

| Cambio | Revisión |
|---|---|
| Commit `4b08705`: `vr` informa por mano `*_hand_active` / `*_hand_valid` / `*_hand_tracked` (sección siguiente) | correcto: pide el estado de la acción de pose y localiza el espacio bajo el lock de entrada; los tests `arms` y `lost` lo cubren. Es lo que hace falta para el diagnóstico de los controles reales del Quest |
| `test_vr_game.py`: `vr` con 60 s de espera | correcto: una lectura de GPU para las capturas de fake_xr puede frenar el presentador más de los 10 s por defecto |
| `test_vr_bow.py`: `--capture-every` (30 por defecto) y 60 s de espera | correcto: con capturas continuas el arco no llegaba al disparo |
| Jugado por Codex (fake_xr): caminar 5,64 m, dirección relativa a la vista con la cabeza girada 90°, agacharse 1,55 → 0,88 m; arco: la flecha siguió la línea de las manos (−19,5° contra −20,1° esperados), puntos de trayectoria solo tensado; runas por la mano (tres alturas, cielo / suelo) y por la cabeza; runa desde la cadera izquierda + gatillo derecho como A | coincide con lo medido acá |
| Scripts de exploración del caballo (`build/vr/codex_horse_*.py`, locales) | encontraron caballos en el establo Dueling Peaks pero no llegaron a montar: los del corral no se montan |

### A caballo (jugado, fake_xr, estéreo)

Herramienta nueva `tools/vr/play_horse.py <partida>`: aparece junto al establo Dueling Peaks, busca el caballo libre
más cercano (posición en vivo por `SAVAGEWIND_VR_LOG_MODELS=Horse_Link`), aparece agachado detrás de él según hacia
dónde se movía, presiona A y lo calma con L si corcovea, hasta que los flags digan "a caballo"; después cabalga.

| Qué | Resultado |
|---|---|
| Montar un caballo salvaje | logrado (a los ~13-28 intentos: huyen). Los del corral del establo no se montan |
| Bit "a caballo" | **confirmado en Switch**: flags `0x100000` (1 << 20), como el enum de BetterVR |
| Caminar con el stick | 7,4-12,7 m en 3 s |
| Espuela (A derecho, BetterVR) | más rápido: 11 m en 2 s |
| Freno (grip izquierdo = B) | 0,02-0,04 m/s en menos de un segundo |
| Altura del ojo montado | en la cabeza real de Link: ~2,9 m sobre el suelo (posición del jugador a 2,35 m, la cabeza 0,57 m más arriba); el giro del cuerpo no se aplica a caballo |
| Imagen del visor montado | pasto, árboles y las manos; el cuello del caballo no entra porque la cabeza simulada mira hacia donde mira la cámara, no hacia donde va el caballo |

Observado, sin cambiar: el grip izquierdo mantenido frena y, con el caballo ya quieto, el juego desmonta (en BOTW B
detenido = bajarse; BetterVR mapea igual). El palmeo para calmar (L) no se llegó a ver en el juego: Link se había
bajado antes (sigue cubierto por el test unitario). Un caballo salvaje recién montado tira a Link a los pocos
segundos si no se lo calma.

## 2026-11-20 (diagnóstico de controles)

- El comando `vr` ahora separa, para cada mano, acción de pose activa, pose válida y pose rastreada
  (`left_hand_*`, `right_hand_*`). Una sesión FOCUSED podía decir `controllers: true` aunque los controles de Quest no
  dieran posición. La simulación de manos no altera estos campos: muestran el estado de los controles reales.
- Probado: `make VR=1 -j8` y escenario `arms` de `test_vr_game.py` con `fake_xr`: ambas manos activas, válidas y
  rastreadas; 46 cuadros con los dos brazos resueltos, error máximo de muñeca 0,000 m. El escenario `lost` confirmó
  los seis campos en `false` tras perder la sesión y que el juego sigue en plano; `make VR=1 test-vr` pasó (1901
  comprobaciones). Falta verificar esos campos con los controles reales de Quest en la mano.

## 2026-11-19 / 20

### Probado en el visor real (Meta Quest 3 por Virtual Desktop, RTX 3090)

| Qué | Resultado |
|---|---|
| Crash "device lost" (GPU) a los pocos segundos de empezar | **Arreglado y confirmado**: ~3 min en dos sesiones sin pérdida del dispositivo (antes, segundos). Causa: Virtual Desktop envía sus propios timers de GPU en la cola de Vulkan de la sesión dentro de `xrBeginFrame`, sin sincronizar con nuestro hilo de grabación. Ahora todas las llamadas OpenXR que pueden enviar trabajo van bajo el lock de la cola (`XRQ`, runtime/vr/xr_vulkan.c) |
| Sesión | FOCUSED, 90 fps estables |
| Estéreo | 90 pares por segundo (el juego a ~180 fps internos) |
| Catalejo | pasa a la pantalla virtual y vuelve al 3D |
| Brazos con manos simuladas sobre el visor real | ambas muñecas en su objetivo (0 cm) |
| Controles reales | **sin verificar**: en esas pruebas Virtual Desktop no dio la posición de ningún control (¿dormidos o no sostenidos?) |

Con el visor apoyado (sensor de proximidad descubierto) la sesión queda en SYNCHRONIZED y no se muestra: es del
visor, no del juego.

### Herramientas nuevas

- **Simulador de manos** `tools/vr/handsim.py`: arranca el juego en VR y mueve cabeza y manos (posición y giro)
  mientras corre, por el archivo en vivo de fake_xr (`FAKE_XR_LIVE`). Poses predefinidas (incluida la pose real del
  Quest en reposo), `hand` / `handq` / `rel`, `move` (movimiento suave, con velocidad real), `tap`, `draw` (sacar el
  arma), `sweep` (girar una mano por pasos con captura), `shot` / `xshot` (ventana / lo que recibe el visor), `arms`
  (error de muñecas, flexión, giro del antebrazo, giro del cuerpo), `focus` / `unfocus`, `game <comando>`.
  ```
  python tools/vr/handsim.py --save <partida de prueba> "pose ahead" "wait 1" "shot ahead" arms
  ```
- **Modo híbrido sobre el visor real**: `--runtime-json <json de Virtual Desktop>`: el visor pone cabeza e imagen y las
  manos simuladas entran al juego por `SAVAGEWIND_VR_SIM_HANDS` (solo para los brazos; gestos y combate leen los
  controles reales).
- **Sonda** `build/vr/xrprobe.exe` (local): con `XR_RUNTIME_JSON` del runtime imprime `system 0` si hay visor,
  `system -35` si no.
- **Logs de desarrollo**: `SAVAGEWIND_LOG_NEW_FS` (cada shader la primera vez que dibuja), `SAVAGEWIND_WATCH_FS`
  (cuándo dibujan ciertos shaders), `SAVAGEWIND_LOG_SCREENS` ahora marca con `*` las pantallas que dibujan; el `vr`
  de control reporta `move_flags`.

### Cambios (todos con tests)

| Cambio | Por qué / cómo se encontró |
|---|---|
| Lock de cola OpenXR en todas las llamadas que envían | el crash de Virtual Desktop |
| Giro del antebrazo y codo que sigue a la mano | con el simulador: girar la mano 90° doblaba la muñeca; ahora el giro va al antebrazo; el codo se queda abajo (sin "alas de pollo") |
| El cuerpo de Link gira hacia donde mirás (como BetterVR) | corriendo de costado los brazos quedaban hasta 20 cm cortos; ahora 0. No al trepar, nadar, planear, montar o caer |
| Ancla "fresca" de cámara para brazos, arco y armas | como BetterVR: el esqueleto se calcula antes que la cámara del cuadro |
| El ojo sigue la cabeza de Link (agacharse, nadar, montar) | agachado la vista quedaba a altura de parado (1,55 → 0,9 m); el balanceo al caminar se filtra |
| El ojo adelante de la cabeza hacia donde mirás | mirando 180° atrás se veía la espalda de Link |
| Catalejo en la pantalla virtual con el zoom del juego | en VR se perdía el zoom y se veía borroso; detectado por 3 shaders de la lente (otros 2 también dibujan al apuntar Stasis, se descartaron) |
| Sin profundidad de campo ni destellos del sol en el visor | como BetterVR 1.0.1 |
| Escudo con la mano izquierda solo con escudo + arma de una mano, desde altura de pecho | antes fijaba objetivo (ZL) con la mano vacía |
| Trepando/planeando: A suelta al instante. A caballo: A, grip derecho acelera, izquierdo frena | como BetterVR 1.0.1 |
| A caballo: palmear el cuello con la mano vacía lo calma (L) | idea propia; solo test unitario (no hay caballo cerca de la partida de prueba) |
| Sacar un arma no cuenta como ataque (0,7 s) | jugando con el simulador: desenvainar del hombro se leía como estocada |
| Eventos antes de que Link esté en el mundo, en 3D | la suite completa detectó que la escena inicial del juego nuevo iba a pantalla plana |
| El giro del cuerpo solo a modelos pegados a un hueso de Link | revisión propia: un arma enemiga cerca podría haber girado con él |

### Ingeniería inversa

- **Flags de movimiento del jugador**: objeto jugador (`PlayerInfo + 0x60`) `+0xcec` en 1.9.0, mismos bits que el
  `PlayerMoveBitFlags` de BetterVR en Wii U (agachado `1<<2`, en el aire `1<<1`, trepando `1<<4|1<<7`, nadando
  `1<<10`, planeador `1<<15`, ragdoll `1<<19`, caballo `1<<20`). Encontrado comparando el objeto parado, agachado,
  saltando y trepando. Verificados: agachado, salto, trepar; el resto se toma del enum.
- **Catalejo**: no tiene pantalla de UI propia; se detecta por los shaders de su lente.

### Jugado con el simulador (estéreo, fake_xr)

Caminar (19 m), girar, saltar; arco desde el hombro izquierdo, tensado y disparado apuntando con las manos (arco de
trayectoria visible); Stasis apuntado con la mano a izquierda y derecha; espada: dos tajos y dos estocadas detectados
(potencia hasta 1,0); inventario y menú rápido de runas en la pantalla virtual; selección de Magnesis; trepar y soltar
con A; quitarse y ponerse el visor (foco).

### Tests

Suite completa (`build/vr/run_suite.sh`: test_vr_game + 19 tests) en verde tras arreglar `events`. Nuevos: escenario
`scope`, chequeo de agacharse en `firstperson`, Stasis no va a pantalla plana, archivo en vivo de fake_xr en
`test_fake_xr_session`, gestos de trepar / caballo / palmeo en `test_vr_gestures`, regla del escudo en
`test_vr_melee`.

### Pendiente

- Probar los **controles reales** del Quest (una pasada de 1 minuto con los controles en la mano).
- No probado en el juego por falta de objetos en la partida de prueba: escudo (el arma es a dos manos), Magnesis sobre
  metal, caballo, nadar, planeador.
- Mirando muy abajo (70°) asoma el interior del cuello de la túnica; con el movimiento real del cuello es menor.
- La cámara rune (foto) no se pudo probar: la partida no la tiene.
