# Tabla de features de VR

Cada feature de este mod, de BetterVR (la referencia, para el juego de Wii U en Cemu) y de Savage Wind (el recomp
de PC en el que se apoya) que importa en VR, con tres controles cada una. Esta tabla es la lista de control del
proyecto: se genera desde [features.py](features.py) (las filas se editan ahí y se corre `python3 docs/vr/features.py`).
In English: [FEATURES.md](FEATURES.md).

- **Implementado**: ✅ hecho, 🟡 en parte, ❌ todavía no, ➖ acá no hace falta.
- **Validado IA**: ✅ comprobado por una prueba automática (el runtime fake_xr, el simulador de manos, un test
  unitario o un perfil; nunca un visor), 🟡 en parte, ⬜ todavía no.
- **Validado humano**: lo que vio el jugador con el visor puesto. ⬜ sin probar, ✅ anda, ❌ falla, 🟡 en parte. Solo
  lo completa el jugador: probá una fila y reportala por su ID (por ejemplo "C07 falla: B todavía corre"), con una
  captura de la ventana del juego y el `savagewind.log` si podés.
- **Origen**: BVR de BetterVR, Mod propio de este mod, SW de Savage Wind.

Resumen: 193 features; implementadas 173, validadas por IA 145, validadas por humano 4, fallando en el visor 1.

## V. Visor e imagen

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| V01 | Sesión OpenXR: arranca en el visor (Virtual Desktop / SteamVR / Link) | BVR | ✅ | ✅ | ✅ | Abrir con el acceso de VR: el juego aparece en el visor |
| V02 | Sin visor o sin runtime sigue en plano y lo dice | Mod | ✅ | ✅ | ⬜ | Abrir sin Virtual Desktop conectado: juega en el monitor, el log dice por qué |
| V03 | Pantalla virtual para menús, carga y título | BVR | ✅ | ✅ | ⬜ | Pantalla de título: una pantalla flotante adelante |
| V04 | Estéreo real (una imagen por ojo, sin alternar) | BVR | ✅ | ✅ | ⬜ | En juego, cerrar un ojo y otro: profundidad correcta, ojos no invertidos |
| V05 | 6DOF: girar, inclinar, moverse con la cabeza | BVR | ✅ | ✅ | ⬜ | Mirar alrededor, agacharse, asomarse: la vista acompaña sin retraso |
| V06 | Campo visual propio de cada ojo (asimétrico) | Mod | ✅ | ✅ | ⬜ | Bordes de la imagen nítidos, sin estirarse al mirar de reojo |
| V07 | El mundo a velocidad normal en estéreo | Mod | ✅ | ✅ | ⬜ | Pasto, agua y gente a velocidad normal (ni lenta ni rápida) |
| V08 | Resolución de los ojos (1080 a 2160; 1440 por defecto), en vivo | Mod | ✅ | ✅ | ⬜ | Opciones > VR > Resolución de los ojos jugando: más nítido o más rápido al instante, sin reiniciar |
| V09 | Escala de render (Video avanzado) afina la imagen | SW | ✅ | ⬜ | ⬜ | Subirla a 150%: más nítido, menos fps |
| V10 | TAA en estéreo (historia por ojo) | Mod | ✅ | ✅ | ⬜ | Anti-aliasing TAA: sin fantasmas de un ojo en el otro, bordes suaves |
| V11 | Sin profundidad de campo ni lens flare en VR (el resplandor, los discos fantasma y la raya del destello no se dibujan: test_vr_eye_match.py en Kakariko al mediodía) | BVR | ✅ | ✅ | ⬜ | Mirar al sol y lejos: sin destellos (sin discos de colores) ni desenfoque, los dos ojos iguales |
| V12 | La ventana del monitor muestra solo el ojo izquierdo | Mod | ✅ | ✅ | ⬜ | Mirar el monitor: no parpadea entre ojos |
| V13 | Sombras del sol estables en los dos ojos | Mod | ✅ | ✅ | ⬜ | Las sombras de los árboles no parpadean |
| V14 | Filas de Video que no hacen nada en VR, ocultas | Mod | ✅ | ✅ | ⬜ | Opciones > Video: no están pantalla completa, monitor, campo visual, fps, VSync |
| V15 | Profundidad para reproyección (SAVAGEWIND_VR_DEPTH=1) | BVR | 🟡 | ✅ | ⬜ | Apagada: con Virtual Desktop se perdía el dispositivo |
| V16 | Si se pierde la sesión sigue en plano | Mod | ✅ | ✅ | ⬜ | Cerrar Virtual Desktop jugando: el juego sigue en el monitor |
| V17 | Upscaler Snapdragon GSR (una pasada, sin historia) | Mod | ✅ | ⬜ | ⬜ | Video avanzado > Upscaler: Snapdragon GSR y una escala de render menor: bordes nítidos, sin fantasmas |
| V18 | Filas de rendimiento en la página VR (overlay de FPS, escala de render, upscaler, su modo o Dinámico, anti-aliasing) | Mod | ✅ | 🟡 | ⬜ | Opciones > VR: cambiarlas jugando con el overlay de FPS y ver cómo se mueven los fps |

## P. Vista y cámara

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| P01 | Primera persona: el ojo en la cabeza de Link | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Vista: Primera persona |
| P02 | Cabeza de Link oculta (cara, pelo, gorro) | BVR | ✅ | ✅ | ⬜ | Mirar abajo y alrededor: no se ve el interior de la cabeza |
| P03 | Tercera persona (la cámara del juego) | BVR | ✅ | ✅ | ⬜ | Vista: Tercera persona; la cabeza mueve la mirada |
| P04 | Distancia de la cámara en tercera persona | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Distancia en tercera persona |
| P05 | Recentrar la vista | BVR | ✅ | ✅ | ⬜ | Comando vr recenter o volver de un menú: la vista mira hacia donde mira Link |
| P06 | Agachado, nadando o a caballo el ojo baja con la cabeza | Mod | ✅ | ✅ | ⬜ | Agacharse (L3): la vista baja; nadar; montar |
| P07 | El cuerpo de Link gira hacia donde mirás | BVR | ✅ | ✅ | ⬜ | Mirar a un costado parado: el cuerpo acompaña, los brazos llegan a las manos |
| P08 | Las vueltas rápidas de la cámara del juego no giran la vista (fijar) | Mod | ✅ | ✅ | ⬜ | Fijar con el gatillo izquierdo: la vista no pega un tirón |
| P09 | Sin volteretas de la vista con golpes fuertes | BVR | ➖ | ➖ | ⬜ | Un golpe fuerte: la vista se mueve pero no da vueltas (acá el ojo siempre está derecho) |
| P10 | Escalando: el ojo afuera de la pared | BVR | ✅ | ✅ | ⬜ | Escalar una pared: nunca se ve el interior de la roca |
| P11 | Nadando: el ojo arriba del agua | BVR | ✅ | ✅ | ⬜ | Nadar: el ojo justo sobre la superficie, siguiendo la cabeza de Link |
| P12 | La mira (tableta sheikah) en la pantalla virtual, con zoom | BVR | ✅ | ✅ | ⬜ | Clic del stick derecho mantenido: la mira en la pantalla plana; soltar: el mundo |
| P13 | Puertas de santuario y actores LinkTag en estéreo | BVR | ✅ | ✅ | ⬜ | Entrar a un santuario: la puerta se abre normal |
| P14 | Cámara en tercera persona sin colisión donde molesta | BVR | 🟡 | ⬜ | ⬜ | Solo la distancia; las paredes todavía empujan la cámara |
| P15 | La cámara montando una foca de arena | BVR | ❌ | ⬜ | ⬜ | No hecho |

## B. Cuerpo y manos

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| B01 | Los brazos de Link siguen los controles (IK de dos huesos) | BVR | ✅ | ✅ | ⬜ | Mover las manos: las de Link están donde las tuyas (las poses de los brazos: B10) |
| B02 | La ropa y la armadura siguen los brazos | BVR | ✅ | ✅ | ⬜ | Ponerse otra armadura: mangas y guantes acompañan |
| B03 | Giro natural de muñeca y antebrazo | Mod | ✅ | ✅ | ⬜ | Palma arriba y abajo: el antebrazo gira, la muñeca no se dobla rara |
| B04 | Codos abajo, sin "alas de pollo" | Mod | ✅ | ✅ | ⬜ | Manos adelante a la altura del pecho: codos hacia abajo |
| B05 | Marcadores de los controles en menús y tercera persona | Mod | ✅ | ✅ | ⬜ | En un menú: un disco celeste en cada mano. Opciones > VR > Mostrar los controles |
| B06 | Lo que Link tiene en la mano se lee del juego | Mod | ✅ | ✅ | ⬜ | Equipar espada, arco, escudo: los gestos saben qué tenés |
| B07 | Antorchas, brazos de bokoblin y ramas como armas | BVR | ✅ | ⬜ | ⬜ | Agarrar una antorcha o un brazo de bokoblin: se usa como espada |
| B08 | La cabeza de Link conserva su sombra en primera persona | BVR | ❌ | ⬜ | ⬜ | No hecho: la cabeza oculta no tiene sombra |
| B09 | Link y su arma no se vuelven transparentes con la cámara cerca | BVR | ➖ | ✅ | ⬜ | No hace falta: en las capturas de primera persona manos y armas se ven sólidas |
| B10 | Poses naturales de los brazos al mover y girar las manos (codo, hombro, giro del antebrazo) | Mod | ✅ | ✅ | ⬜ | Mover las manos, subirlas sobre la cabeza, girar las palmas arriba y abajo: codos y hombros naturales, nada salta (SAVAGEWIND_VR_ARM_RIG=0: el rig anterior, para comparar) |
| B11 | Las manos y brazos de Link a la profundidad correcta en los dos ojos al girar (el esqueleto del cuadro derecho con el ancla del par) | Mod | ✅ | ✅ | ⬜ | Con las manos adelante, girar con el stick: las manos se ven de a una, ni cruzadas ni dobles |

## C. Controles (esquema Quest)

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| C01 | Stick izquierdo: caminar hacia donde mirás | BVR | ✅ | ✅ | ⬜ | Caminar mirando a un lado: va hacia donde mirás |
| C02 | Caminar hacia donde apunta el control izquierdo | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Dirección al caminar: la mano izquierda |
| C03 | Clic del stick izquierdo: agacharse; mantenido: correr | Mod | ✅ | ✅ | ⬜ | Mantener el clic caminando: corre |
| C04 | Stick derecho: girar (suave, por pasos o solo cabeza) | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Giro; probar los tres |
| C05 | Stick derecho arriba: saltar | Mod | ✅ | ✅ | ⬜ | Empujar el stick derecho arriba: salta (no durante Magnesis) |
| C06 | A: aceptar, interactuar | Mod | ✅ | ✅ | ⬜ | Hablar, abrir un cofre con A |
| C07 | B: cancelar, guardar (un toque; mantenido no corre) | Mod | ✅ | ✅ | ⬜ | Mantener B: no corre |
| C08 | Gatillo derecho: atacar con la mano derecha | Mod | ✅ | ✅ | ⬜ | Espada afuera: el gatillo derecho ataca |
| C09 | Gatillo derecho mantenido: ataque cargado (menú de armas: el grip en el hombro derecho; SAVAGEWIND_VR_TRIGGER_MENUS=1: también el gatillo mantenido con la mano vacía) | Mod | ✅ | ✅ | ⬜ | Espada en mano, mantener el gatillo: Link carga, al soltar: el giro |
| C10 | Gatillo izquierdo: cubrirse, fijar y parry al apretar | Mod | ✅ | ✅ | ⬜ | Escudo: apretar justo antes del golpe: parry |
| C11 | Gatillo izquierdo mantenido: la guardia mientras se mantenga (menú de escudos: el grip en el hombro izquierdo) | Mod | ✅ | ✅ | ⬜ | Escudo en mano, mantener el gatillo: sigue cubriéndose, sin menú |
| C12 | A cubriéndote no hace parry | Mod | ✅ | ✅ | ⬜ | Cubierto, apretar A: no hace nada |
| C13 | Grip derecho: sacar o guardar el arco | Mod | ✅ | ✅ | ⬜ | Grip derecho lejos del cuerpo: arco afuera; otra vez: lo guarda |
| C14 | Grip derecho mantenido: menú de arcos | Mod | ✅ | ✅ | ⬜ | Mantener: el menú de arcos; al soltar no sale flecha |
| C15 | Grip izquierdo mantenido con el arco afuera: menú de flechas | Mod | ✅ | ✅ | ⬜ | Arco afuera, mantener el grip izquierdo: el menú de flechas |
| C16 | Grip izquierdo: agarrar, interactuar | BVR | ✅ | ✅ | ⬜ | Grip izquierdo frente a un objeto: lo levanta |
| C17 | Y: la runa; mantenido: menú de runas | Mod | ✅ | ✅ | ⬜ | Y: saca la runa; mantener: el menú |
| C18 | X: el mapa | Mod | ✅ | ✅ | ⬜ | X: abre el mapa |
| C19 | Botón menú: pausa | Mod | ✅ | ✅ | ⬜ | Botón menú izquierdo: el menú de pausa |
| C20 | Clic del stick derecho: inventario; mantenido: la mira | BVR | ✅ | ✅ | ⬜ | Clic: el inventario |
| C21 | En menús los botones son las letras del juego (gatillos L / R) | BVR | ✅ | ✅ | ⬜ | Inventario: los gatillos cambian de página |
| C22 | Un botón mantenido al abrir o cerrar un menú no se repite | Mod | ✅ | ✅ | ⬜ | Abrir el mapa con X mantenido: no se cierra solo |
| C23 | Íconos de botones del Quest en vez de los de Switch | Mod | ✅ | ✅ | ⬜ | Video > Íconos de botones: Automático; los carteles muestran A, B, gatillos |
| C24 | La vibración del juego en los controles | BVR | ✅ | ✅ | ⬜ | Golpear o recibir un golpe: vibra |
| C25 | Fuerza de la vibración | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Vibración y Fuerza de la vibración |
| C26 | Modo zurdo (manos intercambiadas) | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Zurdo: todo pasa a la otra mano |
| C27 | Botones de BetterVR (SAVAGEWIND_VR_CONTROLS=bettervr) | BVR | ✅ | ✅ | ⬜ | B salta, A mantenido corre |
| C28 | Esquema clásico (SAVAGEWIND_VR_CONTROLS=classic) | Mod | ✅ | ✅ | ⬜ | Botones como un joystick, sin gestos |
| C29 | Controles Index, WMR, Vive, Pico, Vive Cosmos y Steam Frame | BVR | ✅ | ⬜ | ⬜ | Solo si tenés esos controles |
| C30 | Un joystick común sigue funcionando junto a los de VR | Mod | ✅ | ✅ | ⬜ | Conectar un control Xbox: también juega |
| C31 | Zona muerta de los sticks | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Zona muerta de los sticks: subila si Link camina solo |
| C32 | Corregir la rotación de los controles (runtime Oculus de Meta) | BVR | ✅ | 🟡 | ⬜ | Opciones > VR > Corregir la rotación de los controles: Automático la activa con el runtime Oculus |
| C33 | Sin sacudida de cabeza cuando no hay nada para sacar | BVR | ✅ | ✅ | ⬜ | Sin arco, el grip derecho o el gatillo no hacen que Link sacuda la cabeza |
| C34 | Escaleras con manejo especial (subir y bajar mire donde mire) | BVR | ✅ | 🟡 | ⬜ | En una escalera, mirar a un costado y empujar adelante: Link sube (el giro probado en llano, todavía no en una escalera) |
| C35 | Un menú rápido no gira la vista (el stick derecho elige en él) | Mod | ✅ | 🟡 | ✅ | Mantener el menú de runas o armas, mover el stick derecho a los costados, soltar: la vista mira adonde miraba |

## G. Gestos y lugares del cuerpo

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| G01 | Hombro derecho + grip: sacar o guardar el arma | BVR | ✅ | ✅ | ⬜ | Mano derecha al hombro, grip: espada afuera; otra vez: la guarda |
| G02 | Hombro derecho, grip mantenido: menú de armas | BVR | ✅ | ✅ | ⬜ | Mantener 0,5 s: el menú |
| G03 | Hombro izquierdo + grip: el arco | BVR | ✅ | ✅ | ⬜ | Mano al hombro izquierdo, grip: el arco |
| G04 | Hombro izquierdo mantenido: escudos (con arma) o arcos | BVR | ✅ | ✅ | ⬜ | Mantener 0,5 s: el menú |
| G05 | Cadera izquierda + grip: la runa; mantenido: las runas | BVR | ✅ | ✅ | ⬜ | Mano a la cadera izquierda |
| G06 | Cadera derecha, grip mantenido: soltar lo de esa mano | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Gesto de soltar; mantener 0,5 s: el arma cae (la tableta nunca) |
| G07 | Lanzar el arma: gatillo mantenido en el hombro, soltar | BVR | ✅ | ✅ | ⬜ | Arma afuera, gatillo en el hombro derecho, soltar: la tira |
| G08 | Silbido: las dos manos en la boca + los dos gatillos | Mod | ✅ | ✅ | ⬜ | Viene el caballo; en la boca los gatillos no cubren ni atacan |
| G09 | Silbido de BetterVR: los dos grips en la boca | BVR | ✅ | ✅ | ⬜ | Esquema bettervr: grips en la boca |
| G10 | Parry físico: un empuje rápido de la mano izquierda cubriéndote | BVR | ✅ | ✅ | ⬜ | Escudo arriba, empujar la mano izquierda adelante: parry |
| G11 | Cubrirse levantando la mano del escudo | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Levantar la mano izquierda para cubrirse; mano frente al pecho: se cubre |
| G12 | Vibración al tocar cada lugar del cuerpo | BVR | ✅ | ✅ | ⬜ | Pasar la mano por el hombro: vibra |
| G13 | Guía de los lugares del cuerpo | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Guía de los lugares del cuerpo: discos celestes en hombros, caderas y boca al acercar la mano |
| G14 | El escudo apuntado con la mano (la guardia mira hacia la vista) | BVR | ➖ | ⬜ | ⬜ | No haría falta si la guardia sigue la vista (BetterVR la toma de hacia dónde mira Link; acá Link gira a la vista al cubrirse): cubierto, ¿bloquea golpes del lado al que mirás? |

## A. Combate

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| A01 | Cortes y estocadas con movimiento (análisis de Acudofy) | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Combate con movimiento; cortar en el aire: Link ataca; estocada: estocada |
| A02 | Movimientos lentos o de muñeca no cuentan | BVR | ✅ | ✅ | ⬜ | Mover la mano despacio: no ataca |
| A03 | Pega donde está el arma (su hitbox sigue la mano) | BVR | ✅ | ✅ | ⬜ | Pegarle a un enemigo al costado: le pega donde está la mano |
| A04 | Pega cuando la movés, más fuerte cuanto más rápido | BVR | ✅ | ✅ | ⬜ | Golpe suave y golpe fuerte: distinto daño |
| A05 | Un golpe por tajo | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Un golpe por tajo: no le pega dos veces al mismo |
| A06 | Cortar árboles | BVR | ✅ | ✅ | ⬜ | Hachazo a un árbol: cae |
| A07 | El tajo también hace el ataque de Link | Mod | ✅ | ✅ | ⬜ | Opciones > VR > Los tajos hacen el ataque de Link (apagado por defecto desde 2026-11-27: su animación movía el cuerpo de Link bajo las manos, "cuatro brazos"): apagado, pega sin la animación |
| A08 | Perfil de tajos fáciles | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Tajos fáciles |
| A09 | Armas a dos manos con las dos manos | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Agarre a dos manos; mandoble afuera, grip izquierdo en el mango: la hoja apunta a la otra mano |
| A10 | Prender fuego, cortar cuerdas con movimiento | BVR | ✅ | ⬜ | ⬜ | Antorcha al pasto; cortar una cuerda |

## R. Arco

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| R01 | Apuntar con las manos (de la cuerda al arco) | BVR | ✅ | ✅ | ✅ | La flecha sale por la línea de las manos, no hacia donde mirás |
| R02 | Arco de trayectoria (puntos amarillos) y sus modos | BVR | ✅ | ✅ | ⬜ | Tensar: se ven los puntos; Opciones > VR > Trayectoria del arco: Completa, Tenue, Solo el punto de mira, Desactivada |
| R03 | La flecha queda sobre el arco al tensar | Mod | ✅ | ✅ | ⬜ | Tensar: la flecha apunta al arco, no a cualquier lado |
| R04 | Al soltar un menú de arco no sale flecha | Mod | ✅ | ✅ | ⬜ | Menú de arcos y soltar: no dispara |
| R05 | Cámara lenta saltando con el arco | SW | ✅ | ⬜ | ⬜ | Saltar desde alto y tensar: cámara lenta |
| R06 | Apuntar con la cabeza | Mod | ✅ | ✅ | ⬜ | Opciones > VR > Apuntar el arco con las manos: apagado, sale hacia donde mirás |
| R07 | La cuerda sigue a la mano que tensa | BVR | ✅ | ✅ | ⬜ | Tensar: la cuerda va a tu mano |

## U. Runas

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| U01 | Runas apuntadas con la cabeza (retícula) | BVR | ✅ | ✅ | ⬜ | Bomba, Stasis, Cryonis apuntan al centro de la vista |
| U02 | El gatillo derecho usa la runa | Mod | ✅ | ✅ | ⬜ | Runa afuera, gatillo derecho: la usa |
| U03 | Magnesis con la mano: izquierda y derecha | BVR+Mod | ✅ | ✅ | ⬜ | Agarrar algo con Magnesis, mover la mano al costado |
| U04 | Magnesis: subir y bajar con la mano | Mod | ✅ | ✅ | ⬜ | Subir y bajar la mano: el objeto acompaña |
| U05 | Magnesis: el stick acerca o aleja | Mod | ✅ | ✅ | ⬜ | Stick derecho arriba y abajo |
| U06 | Magnesis: la vista no gira sola | Mod | ✅ | ✅ | ⬜ | Mover el objeto: la vista queda quieta |
| U07 | Stasis, Cryonis, bombas | SW | ✅ | 🟡 | ⬜ | Usarlas normal |
| U08 | Magnesis: girar el objeto con la muñeca | BVR | ❌ | ⬜ | ⬜ | No hecho |

## I. Objetos

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| I01 | Levantar objetos | SW | ✅ | ⬜ | ⬜ | A frente a una maceta o roca chica: la levanta |
| I02 | Tirar con la velocidad de la mano | BVR | ✅ | ⬜ | ⬜ | Cargándola: gatillo derecho apretado, tirar la mano y soltar: sale hacia donde tiraste |
| I03 | Lo que cargás se ve en la mano | BVR | ✅ | ⬜ | ⬜ | Cargando: lo chico en la mano derecha, lo grande entre las dos manos, achicado |
| I04 | Las ventanas de objetos: el del cofre, el objeto nuevo, la descripción del inventario | Mod | ✅ | ✅ | ✅ | Abrir un cofre; abrir el inventario: los cuadros del tamaño y en el lugar del juego |
| I05 | Montar a caballo | SW | ✅ | ⬜ | ⬜ | Subir y andar: la vista acompaña |
| I06 | Planear, escalar, nadar | SW | ✅ | 🟡 | ⬜ | Probar cada uno |
| I07 | Cocinar, hablar, comprar | SW | ✅ | ⬜ | ⬜ | Cocinar en una olla, comprar en una tienda |
| I08 | Calmar al caballo: montado y con la mano vacía, el gatillo derecho (BetterVR 1.0.2) | BVR | ✅ | ✅ | ⬜ | Montar sin arma afuera y apretar el gatillo derecho: el caballo se calma, no saca el arma; con un arma afuera ataca |
| I09 | Acariciar al caballo: palmear con la mano vacía bajo y adelante (su cuello) | Mod | ✅ | ✅ | ⬜ | Montado, palmear el aire bajo y adelante: Link acaricia al caballo |

## K. Confort

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| K01 | Giro suave y su velocidad | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Velocidad de giro |
| K02 | Giro por pasos y su ángulo | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Ángulo del paso |
| K03 | Giro solo con la cabeza | Mod | ✅ | ✅ | ⬜ | Giro: Solo cabeza; el stick no gira |
| K04 | Viñeta de confort al moverse | Mod | ✅ | ✅ | ⬜ | Opciones > VR > Viñeta de confort: Alta; caminar oscurece los bordes |
| K05 | Sentado o parado | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Posición de juego: Parado usa tu altura real |
| K06 | Ajuste de altura (-30 a +30 cm) | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Altura |
| K07 | Escala de habitación: caminar en la sala camina a Link (la forma simple) | BVR+Mod | 🟡 | ✅ | ⬜ | Opciones > VR > Escala de habitación; dos pasos en la sala: Link camina; contra una pared la vista vuelve |
| K08 | Presets de confort (Suave, Equilibrado, Inmersivo) | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Preset de confort; cambiar una fila después: Personalizado |
| K09 | Escala de habitación completa (el cuerpo de Link movido con sus propios rayos) | BVR | ❌ | ⬜ | ⬜ | No hecho: está la versión simple |

## H. HUD y menús

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| H01 | El HUD aparte, flotando adelante | BVR | ✅ | ✅ | ⬜ | Corazones y minimapa en un panel delante, no pegados a los ojos |
| H02 | El HUD sigue la mirada o queda fijo | BVR | ✅ | ✅ | ⬜ | Opciones > VR > El HUD sigue tu mirada: apagado, queda abajo |
| H03 | Tamaño y distancia del HUD | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Tamaño / Distancia del HUD |
| H04 | Pausa, mapa, inventario en la pantalla virtual | BVR | ✅ | ✅ | ⬜ | Abrirlos: una pantalla flotante |
| H05 | El menú de Savage Wind en la pantalla virtual | Mod | ✅ | ✅ | ⬜ | Abrir con F1 o mantener 0,6 s el botón menú del visor durante el juego |
| H06 | Overlay de rendimiento en el visor | BVR | ✅ | ✅ | ⬜ | Video > Overlay de rendimiento: arriba a la derecha |
| H07 | Opciones de VR dentro del juego | BVR | ✅ | ✅ | ⬜ | Pausa > Sistema > Opciones > VR |
| H08 | Página de guía de controles | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Guía de controles: cada botón y gesto |
| H09 | Diálogos y textos legibles | SW | ✅ | ⬜ | ⬜ | Hablar con alguien: se lee bien en la pantalla |
| H10 | Marcadores de pantalla sobre el HUD flotante (nombres, objetivos) | BVR | ✅ | 🟡 | ⬜ | Fijar un enemigo, poner un pin en el mapa: el marcador queda sobre la cosa, no más al centro (HUD que sigue la mirada) |
| H11 | La rueda de stamina ubicada para el HUD flotante (primera persona: bajo los corazones) | BVR | ✅ | 🟡 | ⬜ | Correr o escalar en primera persona: la rueda bajo los corazones |
| H12 | Arreglos de transparencias de la interfaz | BVR | ➖ | ✅ | ⬜ | No hace falta: el HUD se separa de otra forma; las capturas se ven bien |

## E. Eventos y cinemáticas

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| E01 | Modo de cinemáticas automático (tabla de 1497 eventos de BetterVR) | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Cinemáticas: Automático |
| E02 | Cinemáticas siempre en primera, tercera persona o en pantalla | BVR | ✅ | ✅ | ⬜ | Probar cada modo con un evento (abrir un cofre) |
| E03 | Barras negras para cinemáticas en la pantalla | BVR | ✅ | ✅ | ⬜ | Opciones > VR > Barras negras en cinemáticas |
| E04 | Eventos sin bajada de fps (aparece el cofre, abrirlo) | Mod | ✅ | ✅ | ⬜ | Limpiar un campamento: aparece el cofre a buen fps |
| E05 | Eventos que deben ir a 30 fps (Kohga) en la pantalla | SW+Mod | ✅ | ⬜ | ⬜ | Solo si llegás |

## O. Opciones, instalación y plataforma

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| O01 | Instalador Install BOTW VR.bat | Mod | ✅ | ✅ | ⬜ | Instalar de cero |
| O02 | El lanzador de VR (Savage Wind VR.exe) | Mod | ✅ | ✅ | ⬜ | Abrirlo desde Virtual Desktop |
| O03 | Las opciones se guardan entre sesiones | Mod | ✅ | ✅ | ⬜ | Cambiar algo, reiniciar: sigue |
| O04 | Las opciones de VR en español | Mod | ✅ | ✅ | ⬜ | Idioma español: todo lo de VR en español |
| O05 | El comando vr en la consola (estado) | Mod | ✅ | ✅ | ⬜ | Consola del juego: vr |
| O06 | Linux (AppImage con VR) | Mod | ✅ | 🟡 | ⬜ | Solo si tenés Linux y SteamVR / Monado / WiVRn |
| O07 | Renderizado foveado | BVR | ❌ | ⬜ | ⬜ | No hecho (Virtual Desktop no ofrecía la extensión) |
| O08 | Vista de espectador / grabación (más ancha, sin HUD) | BVR | ❌ | ⬜ | ⬜ | No hecho: el monitor muestra el ojo izquierdo |
| O09 | Arrancar directo al juego | BVR | ➖ | ➖ | ⬜ | No hace falta: el lanzador de VR arranca el juego directo |
| O10 | Ventana que el sistema deja de otro tamaño (Steam Frame standalone, gamescope): los ojos a su resolución y bien fusionados | Mod | ✅ | ✅ | ⬜ | Steam Frame: Opciones > VR > Resolución de los ojos cambia la nitidez; sin fatiga visual |
| O11 | Sin congelamientos por la ventana del monitor: jugando no se actualiza (SAVAGEWIND_VR_MIRROR=1: se muestra) | Mod | ✅ | ✅ | ⬜ | Jugar con la ventana del juego en el monitor, moverla: el visor nunca se congela (antes: hasta 2 s) |
| O12 | Barreras más livianas entre pases de render en VR (SAVAGEWIND_VK_BARRIER_FRAG=0: como antes) | Mod | ✅ | ✅ | ⬜ | Comparar los fps con y sin; la imagen igual (terreno, pasto, agua, efectos) |
| O13 | Las sombras del sol dibujadas una vez por par estéreo: apagado por defecto, las sombras del ojo derecho salían mal (SAVAGEWIND_VR_SHADOWS_ONCE=1: encendido) | Mod | ✅ | ⬜ | ⬜ | Quieto cerca de sombras (árboles, rocas): las mismas sombras en los dos ojos |
| O14 | La capa del HUD hecha una vez por par estéreo | Mod | ✅ | ✅ | ⬜ | El HUD (corazones, minimapa, vigor) se ve y se actualiza como antes |
| O15 | Linux 6.7+: la geometría del juego en memoria de video (SAVAGEWIND_NO_WRITE_WATCH=1: como antes; Windows ya lo tenía) | Mod | ✅ | ✅ | ⬜ | Linux: el log dice "write watch: userfaultfd" y "heap copy in video memory: on" |
| O16 | El HUD del juego dibujado solo en el cuadro del ojo derecho, que arma la capa del HUD (SAVAGEWIND_VR_HUD_ONCE=0: en los dos) | Mod | ✅ | ✅ | ⬜ | El HUD (corazones, minimapa, vigor, indicaciones) se ve y se actualiza como antes; comparar los fps con y sin |
| O17 | Estéreo sincrónico, en desarrollo (SAVAGEWIND_VR_SYNC=1, apagado por defecto): un cuadro del juego por par, sus listas traducidas otra vez para el ojo derecho con su cámara - los dos ojos en el mismo momento, el juego simulado una vez por par. La geometría y la luz están bien, las pasadas que no dependen del ojo se dibujan una vez, las sombras del sol bien en los dos ojos; un árbol lejano negro y a veces el ojo derecho más oscuro (docs/vr/SYNC_STEREO.md) | Mod | 🟡 | ⬜ | ⬜ | Todavía no es para jugar |
| O18 | Multiview (Opciones > VR > Renderizado: Multiview; experimental, no es el modo por defecto): los dos ojos dibujados en una pasada desde un cuadro del juego, el HUD aparte, TAA por ojo, giro suave - 147,5 fps en el visor contra 98,5 en estéreo con TAA (fake_xr, RTX 3090, Ryzen 9 9950X3D, 5 corridas intercaladas). Todavía no tiene: vectores de movimiento por objeto para el TAA, Escalado de Video (Automático elige Estéreo si está activo; Nitidez y Color por ojo desde 2026-11-26), algunas pasadas de reflejos de 64x64 sólo en el ojo izquierdo. 2026-11-25 (después de ese reporte): el HUD en su diseño 16:9 como en Estéreo, el borde exterior del ojo derecho completo (los dos ojos con el campo unión), el ojo derecho ya no recibe la imagen del izquierdo en un tercio de los cuadros (capturas en un Quest 3), igualdad de ojos en Kakariko 0,00-0,18% distinto | Mod | 🟡 | ✅ | ❌ in a headset (2026-11-25) smoke, part of the ground and an enemy marker showed in one eye only; item windows looked stretched up and down (fake_xr: the HUD layer is stretched in multiview only); things lower than they should be (missing terrain: fixed after); snow in a different place in each eye | Jugar un rato en el modo por defecto: las manos y el mundo a la profundidad justa en los dos ojos, ningún ojo más oscuro ni doble, el giro con el stick suave; comparar los fps con Estéreo |
| O19 | Opciones > VR > Renderizado (también en Savage Wind Configuration, el launcher): Automático (recomendado: Estéreo por ahora), Estéreo (el más compatible), Multiview (más rápido, experimental), Mono; se aplica al reiniciar (SAVAGEWIND_VR_RENDER=auto|stereo|multiview|mono) | Mod | ✅ | ✅ | ⬜ | Elegir cada uno y reiniciar: el log dice "[vr] rendering: ..." y cambian la imagen y los fps; Automático dice Estéreo |
| O20 | Mono: una imagen centrada para los dos ojos, sin profundidad estereoscópica (para quienes ven con un ojo, o por velocidad): sólo si se elige, nunca automático. Los dos ojos con los mismos bytes, las vistas desde el centro de la cabeza, una actualización del juego por cuadro (test_vr_mono.py); 178,6 fps en el visor (fake_xr) | Mod | ✅ | ✅ | ⬜ | Opciones > VR > Renderizado: Mono, reiniciar: cómodo, menús y HUD simples y legibles; sin profundidad 3D (como debe ser) |
| O21 | Multiview pedido (o Automático) con un driver que no lo tiene: Estéreo, no el prototipo de estéreo sincrónico (SAVAGEWIND_VK_NO_MULTIVIEW=1 lo prueba) | Mod | ✅ | ✅ | ⬜ | Sólo con una GPU sin multiview: el log dice que el driver no lo tiene y juega en Estéreo |
| O22 | Opciones > VR > Funciones experimentales (apagado por defecto; SAVAGEWIND_VR_EXPERIMENTAL=1): lo experimental sólo corre encendido - hoy Renderizado: Multiview; apagado, Multiview elegido corre en Estéreo y la fila dice que lo necesita | Mod | ✅ | ✅ | ⬜ | Renderizado: Multiview con esto apagado: la fila dice "(requiere Funciones experimentales)" y se juega en Estéreo; encenderlo y reiniciar: Multiview |
| O23 | La ventana del juego en el monitor oculta mientras corre la sesión del visor (Opciones > VR > Ocultar la ventana en el monitor, encendido; vuelve al terminar la sesión; SAVAGEWIND_VR_HIDE_WINDOW=0 la deja) | Mod | ✅ | ✅ | ⬜ | Jugar en el visor: sin ventana del juego en el escritorio; Savage Wind Configuration sigue abierta al lado; desconectar el visor: la ventana vuelve |
| O24 | Rendimiento en VR (Opciones > VR > Rendimiento, encendido por defecto): Distancia de dibujado Potato y SMAA en lugar de TAA solo en el visor, lo elegido en Video se mantiene para el juego en plano; +25% fps del visor en Stereo, +24% en Multiview, +31% en Mono (fake_xr, RTX 3090; SAVAGEWIND_VR_PERFORMANCE=0 lo apaga) | Mod | ✅ | ✅ | ⬜ | Jugar un rato con y sin (no hace falta reiniciar): comparar los fps y a qué distancia aparecen árboles y edificios; el log dice `[vr] performance: on` |

## S. Savage Wind (el recomp de PC) en VR

| ID | Feature | Origen | Implementado | Validado IA | Validado humano | Cómo probar |
|---|---|---|---|---|---|---|
| S01 | Todo el juego y los dos DLC (1.9.0) | SW | ✅ | ⬜ | ⬜ | Usar algo del DLC (Máscara de Majora, la prueba de la espada) |
| S02 | Presets gráficos (Potato a Savage) | SW | ✅ | ⬜ | ⬜ | Video > Preset: cambia la imagen en el visor |
| S03 | Distancia de dibujado, sombras, calidad | SW | ✅ | ✅ | ⬜ | Video avanzado: se nota en el visor |
| S04 | DLSS / FSR 3 / FSR 4 / XeSS | SW | ✅ | ⬜ | ⬜ | Video > Upscaler: ¿se ve bien en los dos ojos? (recomendado apagado) |
| S05 | Generación de frames FSR 3 | SW | ✅ | ⬜ | ⬜ | Probablemente no sirve en VR |
| S06 | HDR | SW | ➖ | ➖ | ➖ | En VR la ventana queda SDR |
| S07 | Gradación de color, filtros de daltonismo | SW | ✅ | ⬜ | ⬜ | Opciones > Color: ¿se aplica en el visor? |
| S08 | Teclado y mouse | SW | ➖ | ➖ | ➖ | No aplica en VR |
| S09 | Giroscopio, Steam Input | SW | ➖ | ➖ | ➖ | No aplica en VR |
| S10 | Perfiles de guardado, backups, importar Wii U / Cemu | SW | ✅ | ⬜ | ⬜ | Cargar una partida importada |
| S11 | Modo foto | SW | ✅ | ⬜ | ⬜ | ¿Funciona con VR puesto? |
| S12 | Trucos, asistencias y teletransporte | SW | ✅ | 🟡 | ⬜ | Teletransportarse: la vista llega bien |
| S13 | amiibo | SW | ✅ | ⬜ | ⬜ | Usar la runa amiibo |
| S14 | Soltar y ordenar en el inventario rápido | SW | ✅ | ✅ | ⬜ | El gesto de soltar lo usa |
| S15 | 30 a 360 fps | SW | ➖ | ➖ | ➖ | En VR los fps los marca el visor (el doble de su Hz) |
| S16 | Discord Rich Presence | SW | ✅ | ⬜ | ⬜ | Opcional |

## Donde este mod va más allá de BetterVR

- Nativo, sin emulador: muchos más fps (unos 110 pares estéreo por segundo a 1080 líneas por ojo en una RTX 3090,
  medido con fake_xr).
- Lo gráfico de Savage Wind: DLSS, FSR, escala de render, distancia de dibujado, sombras, TAA por ojo.
- El juego 1.9.0 con los dos DLC.
- Un esquema de controles propio para Quest, Magnesis arriba y abajo con la mano, la flecha sobre el arco, los íconos
  del Quest.
- Pruebas sin visor (fake_xr, el simulador de manos).
