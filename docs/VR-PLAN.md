# Savage Wind VR — plan de implementación

Objetivo: modo VR (OpenXR, 6DOF, estéreo) dentro de Savage Wind Recomp, con la performance de código nativo, aprovechando
lo aprendido por BetterVR (Cemu). Todo detrás de un flag: sin VR, el juego se comporta exactamente igual que upstream.

Repo: [guideahon/Savage-Wind-VR](https://github.com/guideahon/Savage-Wind-VR), fork de
[Revivedx/SavageWindRecomp](https://github.com/Revivedx/SavageWindRecomp); el trabajo VR va en la rama `vr`, basada en `test`.
Estado de cada fase y cómo correr los tests: [VR.md](VR.md).

---

## 0. Puntos de apoyo que ya existen en Savage Wind

| Qué | Dónde | Para qué sirve en VR |
|---|---|---|
| Hook de cámara `rt_lookat` (eye/at/up de `sead::LookAtCamera` +0x38) | `tools/patches.py` `_lookat_update`, `runtime/hle/freecam.c` | Inyectar la pose de la cabeza. Freecam es casi el mismo problema |
| Sistema de parches sobre código generado (C, con `assert` de la instrucción original) | `tools/patches.py` `PATCHES` | Hooks nuevos: proyección, loop de frame, huesos |
| Sitios por versión (`GAME_SITES`, `game_sites.py`) | `runtime/game_sites.h`, `tools/game_sites.py` | Direcciones de PlayerInfo, pause_state, event_mgr y otras |
| Backend Vulkan propio (instance, device, swapchain, present) | `runtime/nvn/vulkan/device.c` (~l.124, 997, 1570, 1714) | Crear instance/device vía OpenXR y presentar al HMD |
| Separación imagen 3D / HUD (color grading y FSR solo sobre el 3D) | `color_grade.c`, `output_scale.c`, `exec.c` | Capturar el 3D por ojo y el HUD aparte como quad |
| Framepacing y VFR (tiempo real → delta del juego) | `runtime/hle/framepace.c` | Ritmo de frames a cargo de `xrWaitFrame` |
| Motion vectors / TAA | `runtime/nvn/vulkan/mvec.c` | Historial TAA separado por ojo |
| Socket de control + `swctl.py` (`camera`, `look`, `goto`, `shot`, `state`, `fly`) | `docs/CONTROL.md`, `runtime/control*.c` | Base de los tests automáticos con el juego corriendo |
| `perf_routes.py` + `SAVAGEWIND_TEST_WARP` | `docs/PERF-ROUTES.md` | Benchmarks reproducibles en modo VR |
| Tests C sin archivos del juego + CI con filtros por path | `tests/`, `tools/test_*.c`, `.github/workflows/tests.yml` | Mismo patrón para `tests/vr/` |

---

## 1. Arquitectura

```
runtime/vr/
  vr.h              API interna (vr_enabled, vr_frame_begin/end, vr_eye_view, vr_input)
  xr_session.c      instance, system, session, estados, action sets
  xr_vulkan.c       XR_KHR_vulkan_enable2: crea VkInstance/VkDevice para device.c, swapchains por ojo
  xr_frame.c        wait/begin/end frame, layers (proyección + quads)
  vr_math.h         poses, proyección asimétrica, conversión de ejes/unidades (solo cabecera, testeable)
  vr_camera.c       pose de cabeza -> LookAtCamera / proyección (llamado desde rt_lookat y el hook de proyección)
  vr_stereo.c       máquina de estados: tick lógico 1x, render 2x (ojo L, ojo R)
  vr_input.c        acciones OpenXR -> pad virtual del juego (hle/input.c)
  vr_comfort.c      snap/smooth turn, viñeta, recentrado, altura
  vr_body.c         (fase 7) manos/brazos
  vr_melee.c        (fase 8) detección de swing/stab
  vr_trace.c        grabación/reproducción de poses y botones (.jsonl)
tests/vr/           tests unitarios C (sin juego, sin GPU)
tools/vr/
  fake_xr/          runtime OpenXR falso (DLL/.so + manifest JSON) con poses scriptadas
  test_vr_*.py      tests de integración (fake runtime, socket de control)
  vr_routes.py      perf_routes en VR
```

- Build: `make VR=1` define `SW_VR` y enlaza el loader de OpenXR (MSYS2 `mingw-w64-ucrt-x86_64-openxr-sdk`; en Linux
  `libopenxr-dev`). Sin `VR=1` no se compila nada de esto.
- Runtime: `SAVAGEWIND_VR=1` o setting `vr = on` en `savagewind.ini`. Si no hay runtime/HMD, el juego cae a plano y lo
  escribe en el log (`[vr] ...`).
- Unidades: BotW usa 1 unidad = 1 m. OpenXR es diestro, +Y arriba y −Z adelante. sead tiene +Y arriba y yaw 0 = +Z.
  Toda conversión pasa por `vr_math.h`, nunca inline.

---

## 2. Estrategia de pruebas (pirámide)

| Nivel | Qué | Necesita | Dónde corre |
|---|---|---|---|
| **T0 unit** | matemática, máquinas de estado, mapeo de input, detector de swing | nada | CI (Linux + Windows), cada PR |
| **T1 generador** | que `game_sites.py`/`patches.py` encuentren y validen los sitios VR en NSO sintéticos | Python | CI |
| **T2 integración XR** | runtime real contra `fake_xr` + Vulkan por software (lavapipe) con un renderer de prueba, sin juego | Vulkan SW | CI (Linux con `mesa-vulkan-drivers`) |
| **T3 juego en loop** | juego real + `fake_xr` + socket de control: poses scriptadas, screenshots por ojo, asserts | archivos del juego, GPU | local / runner self-hosted (nunca CI público: no se pueden subir archivos del juego) |
| **T4 perf** | `vr_routes.py`: frame time p50/p99 por ruta, gates contra baseline | juego + GPU | local / self-hosted, por release |
| **T5 manual** | checklist en headset real (Quest + VD/ALVR, Index, WMR) | HMD | antes de cada release |

### 2.1 `fake_xr`: el runtime OpenXR falso (pieza clave)

Runtime mínimo cargado con `XR_RUNTIME_JSON=tools/vr/fake_xr/fake_xr.json`:

- Implementa lo que usamos: instance, system, session, `XR_KHR_vulkan_enable2`, swapchains (imágenes Vulkan propias),
  spaces (LOCAL, STAGE, VIEW), `xrLocateViews`, action sets e `xrSyncActions`.
- Las poses y botones se leen de un script `.jsonl` (`{"t":1.5,"head":{"p":[0,1.7,0],"q":[...]},"right":{...},"buttons":{"a":1}}`)
  o se fijan en vivo por un pipe/socket propio (`fake_xr ctl pose ...`).
- Graba lo que el runtime le entrega: cada `xrEndFrame` (cantidad y tipo de layers, FOV, pose por vista, timestamps) y,
  con un flag, un PNG por ojo de la imagen del swapchain.
- Reloj determinista: `predictedDisplayTime` fijo a 90 Hz, configurable a 72/120.
- Inyección de fallos: perder foco, `XR_SESSION_STATE_STOPPING`, `XR_ERROR_SESSION_LOST`, cambio de refresh rate.

Como alternativa o complemento está Monado en modo headless (`XRT_COMPOSITOR_NULL=1`). Pero un fake propio es
determinista y deja hacer asserts sobre los frames, así que es el principal.

### 2.2 Grabar / reproducir (`vr_trace`)

- `SAVAGEWIND_VR_RECORD=trace.jsonl` graba poses y botones reales del headset.
- `fake_xr` reproduce ese mismo archivo. Así un bug visto en headset se vuelve un test T3 reproducible y los swings
  reales se vuelven fixtures T0 del detector de golpes.

### 2.3 Comandos de control nuevos (developer only)

| comando | uso en tests |
|---|---|
| `vr status` | estado de la sesión, refresh, fps por ojo, frames perdidos, modo (estéreo / cine / menú) |
| `vr pose <x y z> <yaw pitch roll>` | fija la cabeza (vía `fake_xr` o override interno) |
| `vr recenter` | recentrar |
| `vr shot <left\|right\|both> <file>` | PNG de cada ojo antes de enviarlo al runtime |
| `vr hud <file>` | PNG del quad del HUD |
| `vr stats` | tiempos CPU/GPU por ojo, para `vr_routes.py` |

### 2.4 Asserts automáticos sobre imágenes (T3)

- **Disparidad estéreo**: la correlación cruzada horizontal L/R tiene que dar un desplazamiento > 0 en objetos cercanos,
  con el signo correcto, y ~0 en el cielo. Detecta ojos invertidos o IPD nula.
- **Coherencia vertical**: el desplazamiento vertical L/R tiene que ser ~0. Detecta errores de rotación o proyección.
- **Head tracking**: `vr pose` con yaw +90 → `camera` reporta yaw +90 ± 1°. Lo mismo con pitch y posición
  (Δ = movimiento de cabeza en metros).
- **Imagen válida**: ni negra, ni NaN/rosa, ni idéntica entre frames consecutivos con la cabeza moviéndose.
- **Sin fugas de TAA**: la imagen del ojo R no contiene ghosting del L (diferencia contra referencia sin TAA por
  debajo de un umbral).
- Imágenes de referencia ("golden") solo locales, por GPU, con tolerancia SSIM. Nunca se suben (son del juego).

---

## 3. Fases

Cada fase tiene: pasos, tests nuevos y un **criterio de salida** que tiene que cumplirse antes de pasar a la siguiente.

### Fase 0 — Base y tooling (1-2 semanas)

1. Fork de `Revivedx/SavageWindRecomp`, rama `vr` creada desde `test`. Rebase semanal sobre `test`.
2. Build normal + `make gen` con tus archivos; correr `perf_routes.py` y guardar el **baseline plano**.
3. `make VR=1`: esqueleto `runtime/vr/` con `vr_enabled()` y log, loader OpenXR enlazado (Windows y Linux).
4. `tests/vr/` con harness mínimo (mismo estilo que `tests/test_*.c`) y targets `make test-vr`.
5. `fake_xr` v0: instance/system/session, sin Vulkan todavía; script de poses estático.
6. Workflow `.github/workflows/vr.yml`: T0 + T1 en Linux y Windows, filtrado por `runtime/vr/**`, `tests/vr/**`,
   `tools/vr/**`.

**Tests:** `test_vr_math` (identidad, conversiones de ejes ida y vuelta, cuaterniones normalizados),
`test_fake_xr_session` (crear/destruir sesión, transición de estados).
**Salida:** CI verde; `make` sin `VR=1` da un binario idéntico en comportamiento (los tests upstream pasan).

### Fase 1 — OpenXR + "cine virtual" (2-4 semanas)

Juego plano en una pantalla virtual. Valida la cadena completa sin tocar el juego.

1. `xr_vulkan.c`: en `device.c`, si VR está activo, crear la instance/device con `xrCreateVulkanInstanceKHR` /
   `xrCreateVulkanDeviceKHR` y elegir el `VkPhysicalDevice` que pide OpenXR (pisa `gpu_select`).
2. Swapchain OpenXR (formato sRGB), quad layer de ~3 m a 2.5 m de distancia.
3. Copiar la imagen final (la misma que va a `vkQueuePresentKHR`) al swapchain XR. La ventana de escritorio queda como
   espejo opcional.
4. Ritmo: en VR, `framepace` usa `xrWaitFrame` (`predictedDisplayPeriod`) en vez de su propio grid. VFR recibe el
   período real.
5. Manejo de estados: perder foco → pausar input; `SESSION_LOST` → volver a plano sin crash.

**Tests:**
- T0 `test_vr_session_fsm`: todas las transiciones de `XrSessionState`, incluidas las de error.
- T2 `test_vr_quad.py`: renderer de prueba con lavapipe + `fake_xr` → 300 frames, un quad layer por frame, imagen no
  negra, timestamps monótonos.
- T2 inyección de fallos: `SESSION_LOST` a mitad → el proceso sigue vivo y el log dice "fallback flat".
- T3 `test_vr_cinema.py`: juego + `fake_xr` → `vr status` = focused; `vr shot both` ≈ `shot` normal (SSIM > 0.95).
**Salida:** jugable en headset real como pantalla flotante, 90 Hz sin frames perdidos en la ruta plateau.

### Fase 2 — Cámara con head tracking (mono) (3-5 semanas)

1. `vr_camera.c` desde `rt_lookat`: para las cámaras de vista (mismo filtro `view_camera` que freecam), combinar la
   cámara del juego (posición y yaw del cuerpo) con la pose de la cabeza (6DOF). El pitch y el roll del juego se
   ignoran: los da la cabeza.
2. Hook nuevo de **proyección** (`sead::PerspectiveProjection`: encontrar el sitio, agregarlo a `game_sites.py` +
   `patches.py` con su `assert`): frustum asimétrico de `XrFovf`, near/far del juego.
3. Desactivar la cámara automática del juego en VR (recentrado, shake, auto-pitch), con opción para el shake.
4. Recentrar (`vr recenter`), altura de piso (STAGE vs LOCAL) y modo sentado.
5. Cutscenes y conversaciones (`event_mgr` activo) → modo cine automático (fase 1). La cámara del evento no se toca.

**Tests:**
- T0 `test_vr_camera`: pose de cabeza + cámara del juego → eye/at/up esperados (tabla de casos: yaw, pitch, roll,
  traslación, cuerpo girado). Proyección asimétrica contra una matriz de referencia calculada a mano.
- T1 `test_vr_sites.py`: los sitios nuevos se encuentran en un NSO sintético y el `assert` del parche falla con una
  instrucción distinta.
- T3 `test_vr_headtrack.py`: `goto plateau` + `vr pose` con yaw 0/90/180/−90 y pitch ±30 → `camera` coincide ±1°;
  traslación 0.5 m → eye se mueve 0.5 ± 0.05.
- T3 cutscene: forzar un evento → modo cine, sin crash, cámara del juego intacta.
**Salida:** mirar alrededor en el headset con un solo ojo duplicado, sin judder, y cutscenes jugables.

### Fase 3 — Estéreo real (4-8 semanas)

Opción A (primero): **un tick lógico, dos renders** (lo que hace BetterVR).

1. Encontrar en el loop de `GameFrameworkNx` la separación update / draw (los mismos sitios que usa `framepace.c`).
   Parche: después del update, correr el draw dos veces con `vr_stereo` eligiendo el ojo en cada pasada. VFR solo
   avanza una vez.
2. Render targets por ojo: capturar la imagen 3D (el punto donde ya se separa del HUD) en un swapchain XR por ojo, o
   uno solo con array layers.
3. Historial temporal por ojo: TAA, motion vectors (`mvec.c`), auto-exposure, bloom con feedback. Duplicar el estado
   o desactivarlo en VR (decidir por medición).
4. Sombras y otros pases independientes de la vista: renderizar una sola vez y reusarlas en el segundo ojo (es la
   primera optimización grande).
5. Efectos de pantalla que no sirven en VR (DOF, viñeta, haze fuerte) apagados por defecto, usando lo que ya existe
   en Advanced options.

Opción B (investigación, en paralelo cuando A funcione): **multiview** (`VK_KHR_multiview`) inyectando `gl_ViewIndex`
en los shaders Maxwell traducidos (`runtime/nvn/maxwell/translate.c`) para los pases de geometría. Documentar el
hallazgo en `docs/research/vr-multiview.md`.

**Tests:**
- T0 `test_vr_stereo_fsm`: secuencia update → draw L → draw R → submit; nunca dos updates seguidos; el ojo correcto
  en cada callback; manejo de un frame perdido.
- T0 `test_vr_eye_history`: el estado TAA del ojo L nunca se lee en el R (doble buffer con IDs).
- T3 `test_vr_disparity.py`: disparidad horizontal positiva en primer plano, ~0 vertical (4 rutas).
- T3 `test_vr_ghosting.py`: ojo R contra un render de referencia sin TAA.
- T3 velocidad del mundo: el reloj del juego avanza igual en VR que en plano (`state` hour a lo largo de 60 s, ±1 %).
- T4 `vr_routes.py`: primer baseline VR por ruta.
**Salida:** estéreo correcto en headset (sin "ojos cruzados" ni ghosting) y ≥ 72 Hz estables en plateau con hardware de
referencia.

### Fase 4 — HUD, menús, UI (2-3 semanas)

1. HUD (la parte que se dibuja después del 3D) → quad layer anclado a la cabeza con lag suave, o fijo al cuerpo
   (configurable). Distancia y escala ajustables.
2. Menús (`pause_state` = 2), mapa, inventario, pantalla de título, carga → modo cine con un quad grande. Sin
   estéreo, para ahorrar GPU.
3. Overlay ImGui de Savage Wind (F1/F10) → quad propio; setting "VR" en Options (vía el sistema de opciones existente).

**Tests:**
- T0 `test_vr_layer_select`: estado del juego (play / pausa / evento / carga / título) → set de layers esperado.
- T3 `test_vr_menus.py`: abrir y cerrar pausa 50 veces → siempre vuelve a estéreo (el bug conocido de BetterVR es la
  pantalla negra al salir de menús; este test existe para evitarlo).
- T3 `vr hud`: el HUD no aparece dentro de los ojos 3D (diferencia contra una captura con el HUD oculto).
**Salida:** juego completo navegable en VR sin quitarse el headset.

### Fase 5 — Input de controles VR (2-4 semanas)

1. Action set OpenXR con binding por perfil (Touch, Index, WMR, Vive, Pico) y valores por defecto razonables.
2. `vr_input.c` → pad virtual de `hle/input.c` (los mismos caminos que el mando). Así el juego recibe input como si
   fuera un controlador.
3. Locomoción relativa a la cabeza o a la mano (setting), snap/smooth turn (`vr_comfort.c`).
4. Menú VR (mantener X, como BetterVR) para settings rápidos.
5. Prompts de botones: íconos según el perfil VR (sistema de íconos existente, #469).

**Tests:**
- T0 `test_vr_input_map`: tabla acción OpenXR → botón del juego, por perfil; deadzones; snap turn exacto (30°/45°).
- T2 `fake_xr` script de botones → lo que recibe el pad virtual (capturado en un stub).
- T3 `test_vr_input.py`: script "caminar 3 s adelante" → `pos` se mueve en la dirección de la cabeza; "A" frente a un
  cofre → `actlog` registra la interacción.
**Salida:** el Gran Plateau jugable de punta a punta con controles VR (sin manos visibles todavía).

### Fase 6 — Confort (1-2 semanas)

Viñeta al moverse o girar, altura de jugador, modo sentado, dirección de caminata, desactivar el head-bob, recentrar
con doble toque.
**Tests:** T0 por cada función (curvas de viñeta en función de la velocidad angular, etc.). T5 checklist de confort.

### Fase 7 — Manos y brazos (2-4 meses, la parte de ingeniería inversa)

1. Mapear con el decomp (Switch 1.5.0) a 1.9.0: el skeleton de Link, el update de huesos post-animación, los slots de
   armas y escudo. Sitios nuevos en `game_sites.py`, cada uno con su test T1.
2. IK de dos huesos (hombro-codo-mano) desde la pose del control; el resto del cuerpo queda animado por el juego.
3. Arma/escudo siguen la mano (la colisión del arma sigue al hueso).
4. Agarrar y soltar objetos, gestos de equipar (hombro = arma, espalda = arco) y lanzar.
5. Portar la lógica de BetterVR donde sirva (MIT: atribución en `third_party/BetterVR/LICENSE` y en los créditos).

**Tests:**
- T0 `test_vr_ik`: la mano alcanza el objetivo dentro del alcance; fuera del alcance el brazo queda estirado sin NaN;
  el codo no se invierte (tabla de poses).
- T0 `test_vr_gestures`: trazas grabadas (`vr_trace`) → gesto detectado / no detectado. Se exigen precisión y recall
  mínimos sobre el set de fixtures.
- T3 `test_vr_hands.py`: `vr pose` de mano a una posición → el hueso de la mano (leído por un comando nuevo
  `vr bone hand_r`) queda a ≤ 2 cm.
**Salida:** manos visibles siguiendo los controles y armas en la mano.

### Fase 8 — Combate con movimiento (1-3 meses)

1. Detector de swing/stab (velocidad y aceleración de la punta del arma, dirección, umbral por tipo de arma) →
   dispara el ataque del juego con la dirección correspondiente. Basado en el sistema de Acudofy (BetterVR).
2. Arco: apuntado con dos manos, tensión según la distancia entre manos.
3. Escudo levantado según la pose de la mano; parry por gesto.
4. Opción "solo un hit por swing" y botón de ataque clásico como fallback.

**Tests:**
- T0 `test_vr_melee`: dataset de trazas reales grabadas (swings, stabs, movimientos normales que *no* son ataques) →
  matriz de confusión con umbrales mínimos (p. ej. ≥ 95 % de swings detectados, ≤ 2 % de falsos positivos).
  Corre en CI.
- T0 `test_vr_bow`: geometría de apuntado → dirección y tensión.
- T3 `test_vr_combat.py`: `goto` a un bokoblin + reproducir la traza de un swing → el enemigo recibe daño
  (leído del actor).
**Salida:** combate cuerpo a cuerpo y arco jugables sin botones.

### Fase 9 — Rendimiento (continuo, foco después de la fase 3)

1. `vr_routes.py` (= `perf_routes.py` + `fake_xr` a 90 Hz + `vr stats`): p50/p99 de CPU y GPU por ojo y frames
   perdidos.
2. Gate: un PR que empeora el p99 > 5 % en cualquier ruta no se mergea a `vr` (solo local/self-hosted).
3. Optimizaciones en orden de retorno esperado:
   - compartir sombras y pases view-independent;
   - multiview (fase 3B);
   - render scale por ojo y FSR 1/3 por ojo (ya existen en el runtime);
   - foveated rendering fijo (`VK_EXT_fragment_density_map` o VRS);
   - menos draw distance en VR por preset;
   - reproyección del runtime con motion vectors (`XR_FB_space_warp`) usando `mvec.c`.
4. Comparar contra BetterVR en Cemu con el mismo PC y lugares parecidos, y publicar los números.

**Salida (meta del proyecto):** 90 Hz estables en Hyrule Field con un Ryzen 5 5600X + RTX 3060 a resolución nativa del
Quest 3.

### Fase 10 — Release

1. Setting VR en Savage Wind Configuration, launcher que detecta el runtime OpenXR y guía de instalación.
2. `docs/vr.md`: instalación, controles, troubleshooting (ALVR/VD/Steam Link vs Link, igual que BetterVR).
3. Checklist T5 completo en ≥ 3 headsets.
4. Upstream: proponer a Revivedx primero los hooks genéricos (proyección, loop update/draw, `vr`-agnostic), después el
   módulo VR detrás de `VR=1`. Si no lo quieren upstream, mantener el fork con rebase automatizado sobre `test` y CI
   que avise de los conflictos.

---

## 4. CI concreta

`.github/workflows/vr.yml`:

```yaml
jobs:
  unit:            # T0, Linux + Windows (MSYS2 UCRT64)
    run: make VR=1 test-vr
  generator:       # T1
    run: python3 tools/vr/test_vr_sites.py
  xr-integration:  # T2, Linux, Vulkan por software
    steps:
      - run: sudo apt-get install -y mesa-vulkan-drivers libopenxr-dev
      - run: make VR=1 fake-xr vr-testapp
      - run: VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
             XR_RUNTIME_JSON=build/fake_xr.json python3 tools/vr/test_vr_quad.py
```

T3/T4 en un **runner self-hosted** propio (tu PC, con los archivos del juego fuera del repo):
`tools/vr/run_game_tests.py --game extracted/ --suite smoke|full|perf`. Produce un reporte HTML con capturas por ojo,
diffs y tiempos. Nada de eso se sube como artefacto público.

Suites:
- **smoke** (~5 min, en cada PR a `vr`): cinema, headtrack, disparity en plateau, menús 10×.
- **full** (~1 h, nightly): todas las rutas, todas las pruebas T3.
- **perf** (por release): `vr_routes.py` contra el baseline.

---

## 5. Riesgos y mitigación

| Riesgo | Mitigación |
|---|---|
| Upstream cambia rápido (alpha) y rompe sitios o parches | `assert` en cada parche, T1 en CI, rebase semanal, hooks mínimos e intrusión baja |
| Mecánicas que fallan > 30 fps (#425) se agravan a 90 Hz | Correr el tick lógico desacoplado (p. ej. 60) con render a 90 + reproyección; tests de reloj del mundo |
| El render doble rompe efectos con estado (TAA, exposure, partículas que se actualizan en el draw) | Inventario de pases con estado en la fase 3, doble buffer o desactivar; test de ghosting |
| La GPU duplica el costo | Compartir pases, multiview, foveated, render scale |
| La ingeniería inversa de manos y combate es lenta | Empezar con lo jugable (fases 1-6) sin manos; manos y combate son incrementales |
| No se pueden testear en CI público cosas del juego | Separación estricta T0-T2 (CI) vs T3-T4 (self-hosted); traces y fixtures sin datos del juego |
| Legal | Ni archivos del juego, ni código generado, ni golden images en el repo (misma política que Savage Wind) |

---

## 6. Orden de los primeros PRs

1. `vr: build flag, loader OpenXR, esqueleto y tests de vr_math` (F0)
2. `vr: fake_xr runtime v0 + CI` (F0)
3. `vr: instance/device vía XR_KHR_vulkan_enable2` (F1)
4. `vr: modo cine con quad layer + pacing por xrWaitFrame` (F1)
5. `vr: comandos de control vr status/shot` (F1, habilita T3)
6. `vr: head tracking en rt_lookat + hook de proyección` (F2)
7. `vr: estéreo con un tick y dos renders` (F3)
