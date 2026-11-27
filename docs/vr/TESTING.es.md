# Casos de regresión repetibles para VR

La parte útil del flujo de un juego grabado es poder repetir la misma secuencia de entradas y comparar observaciones
explícitas. Acá `fake_xr` ya ofrece un reloj de pantalla determinista y entradas guionadas para cabeza, manos y
controles. Un test del juego agrega un estado inicial conocido y comprueba el comportamiento VR expuesto por el juego.
Esto no vuelve determinista todo BOTW: cada caso debe declarar su preparación y comprobar solo el comportamiento que
quiere cubrir.

## Herramientas existentes

- `tools/vr/fake_xr/fake_xr.c` lee `FAKE_XR_SCRIPT`. Los tiempos son segundos desde el primer cuadro de pantalla;
  `head` fija la pose de la cabeza y `hand` agrega una pose interpolada de la mano. Por ejemplo:

  ```text
  0.00 head 0 1.70 0 0 0 0
  0.00 hand right 0.20 1.35 -0.40 0 0 0
  0.50 hand right 0.20 1.35 -0.40 0 0 90
  ```

- `tools/vr/test_vr_game.py` inicia cada escenario con una copia de una partida, un reloj de prueba y el runtime
  guionado. Casos específicos como `test_vr_arm_rig.py` y `test_vr_ladder.py` guardan su preparación y sus aserciones
  junto al test.
- `tools/vr/handsim.py` sirve para explorar una pose reportada de forma interactiva. Después, pasá el hallazgo a un
  test guionado para poder repetirlo tras el arreglo.

## Agregar o actualizar un caso

1. Vinculalo con el ID de la feature y describí un comportamiento mediante **preparación → entrada → observación
   esperada**. Incluí la secuencia mínima que reproduce el problema, qué debería pasar y qué pasó.
2. Hacé repetible el estado inicial: anotá versión del juego, build/commit, requisitos de la partida temporal, warp de
   prueba, frecuencia del runtime falso, opciones y variables de entorno relevantes. No agregues partidas al repo.
3. Poné los cambios de cabeza, manos y botones en la línea de tiempo virtual de fake_xr. Evitá que las aserciones de
   tiempo dependan de pausas del reloj real. Indicá cualquier dependencia del juego que el reloj falso no controle.
4. Comprobá la observación más acotada que sirva (por ejemplo, distancia muñeca-control, un estado del juego, una capa
   OpenXR o una acción del mando). Indicá las tolerancias numéricas y qué protegen. No uses igualdad de píxeles entre
   GPUs como oráculo de jugabilidad; dejá las capturas para revisión visual local o usá una métrica documentada.
5. Cuando sea posible, verificá que el caso detecte la regresión. Un control negativo existente es
   `python tools/vr/test_vr_arm_rig.py --save <partida temporal> --old`: se espera que el rig anterior falle el barrido
   de muñeca. No dejes una mutación deliberada en el código de producción.
6. Guardá los resultados bajo una carpeta ignorada como `build/vr/<caso>`. Las capturas leen imágenes desde la GPU y
   pueden frenar el juego: desactivá capturas periódicas durante mediciones de tiempo y pedí solo los cuadros útiles
   para revisar lo visual.

Cada aserción debería imprimir el valor medido y su límite para que el fallo señale la primera diferencia útil. Mantené
separada la evidencia automática de la evidencia en visor: marcá **Validado IA** solo cuando pase el caso automático y
**Validado humano** solo después de que una persona confirme el comportamiento en el visor. Un script puede medir la
geometría de los brazos; no puede confirmar que al jugador le resulten naturales.

## Funciones experimentales y releases

Un cambio que puede empeorar el juego mientras se termina (una ruta de render más rápida, un efecto nuevo, otra forma de
ubicar las manos) entra detrás de **Opciones > VR > Funciones experimentales** (`vr_experimental`, apagado por defecto;
`SAVAGEWIND_VR_EXPERIMENTAL=1`) o de una opción propia apagada por defecto. Apagado, el juego usa lo probado aunque la
opción experimental esté elegida (el log lo dice). Un release nunca deja una ruta experimental como predeterminada; pasa
a serlo cuando sus pruebas automáticas (para el render: `test_vr_eye_match.py`, `test_vr_stereo_sync.py`) pasan y una
persona la vio en el visor. Las notas de cada release y [CHECKLIST.es.md](CHECKLIST.es.md) dicen qué probar y qué
ignorar en ese build.

## Ejemplo: rig de brazos (B01 / B10)

El reporte en visor dijo que las muñecas seguían los controles, pero los brazos de Link se veían poco naturales. El caso
guionado recorre once poses y gira la muñeca derecha de -150° a +150°. Comprueba que la muñeca quede a menos de 2 cm,
que cada paso de 30° mueva el codo menos de 6 cm, que el barrido completo no supere 12 cm, que el giro del antebrazo no
tenga saltos mayores a 60° y que clavículas/codos reaccionen a una pose sobre la cabeza. `--old` muestra que el barrido
detecta el rig anterior. Estas mediciones respaldan el estado automático; el reporte en visor sigue siendo evidencia
separada para la apariencia natural.

Usá entradas sintéticas. Nunca subas al repo archivos del juego, código generado, partidas ni capturas de prueba o de
referencia; las capturas o clips breves sí se pueden adjuntar a un issue según las
[reglas de copyright](../../CONTRIBUTING.md). Reportá por ID; ver [la lista para probar en el visor](CHECKLIST.es.md).

## Antes de cada release: la suite de regresión

`tools/vr/regress.py` (`make vr-regress VR=1 B=build/native-vr DEPS=build/linux/x86_64/deps VR_SAVE=<la partida 100%>`)
corre cada prueba que alguna vez encontró un error, cada una con el error que vigila (`--list`). `--quick`: los casos de
Estéreo y los tests unitarios (~15 min); sin él, todos los modos (~45 min). Un release se hace solo si pasa, y cada
error que una prueba encuentra suma su caso ahí: el caso de un error arreglado se queda, para que no vuelva sin que se
note. Los mínimos de fps son los de la PC de referencia (`--no-perf` en otras). Lo que fake_xr no puede mostrar (cuadros
perdidos, la predicción propia de un runtime, la comodidad) sigue necesitando un visor; `FAKE_XR_HAND_DRIFT` en fake_xr
da a las ubicaciones de manos más tardías poses más tardías, como un runtime.
