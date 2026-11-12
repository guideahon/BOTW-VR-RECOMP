# BOTW VR Recomp: instalación desde cero

*English: [SETUP.md](SETUP.md)*

Este repositorio es Savage Wind Recomp (port nativo de PC de *Breath of the Wild* 1.9.0, compilado desde tu propio
juego) con un modo PC-VR por OpenXR: estéreo, head tracking 6DOF, HUD flotante, controles VR y combate con movimiento.
**No incluye archivos del juego, keys ni firmware**: necesitás tu propia copia, volcada de tu propia Nintendo Switch.

## 1. Qué necesitás

- Tu *Breath of the Wild* (cartucho o eShop) con la **actualización 1.9.0**, en una Switch tuya con homebrew.
  Opcional: los dos DLC de tu propio Pase de Expansión.
- Windows 10/11 x86-64, GPU con Vulkan 1.3 (probado en RTX 3090), ~20 GB libres.
- Visor PC-VR con runtime OpenXR: Quest 2/3/Pro por Virtual Desktop (VDXR o SteamVR), Quest Link / Air Link, SteamVR,
  WMR.

## 2. Volcar tu propio juego

Herramientas comunes de la comunidad; este proyecto no incluye ninguna. Respetá las leyes de tu país.

1. En tu Switch con homebrew: volcá el juego **y** su update 1.9.0 con
   [nxdumptool](https://github.com/DarkMatterCore/nxdumptool) (como NSP, o directo a ExeFS / RomFS con el update
   aplicado: sus opciones de ExeFS y RomFS dejan elegir el update instalado).
2. Si volcaste NSP: sacá las keys de tu consola (`prod.keys`) con
   [Lockpick_RCM](https://github.com/shchmue/Lockpick_RCM) y extraé en la PC con
   [hactool](https://github.com/SciresM/hactool) o con el menú "Extract data" de un emulador (base + update, para que
   el RomFS quede combinado).
3. Necesitás dos carpetas:
   - **ExeFS** de 1.9.0: `main`, `main.npdm`, `rtld`, `sdk`, `subsdk0`
   - **RomFS** con el update 1.9.0 aplicado (base + update), ~41.062 archivos
   - opcional: el RomFS de cada DLC, por separado (`aoc1/`, `aoc2/`)

Nunca compartas estos archivos, tus keys ni el código del juego compilado (`generated/`, `build/swgame.dll`).

## 3. Dónde va cada carpeta

```
BOTW-VR-RECOMP/            este repositorio
  extracted/               (git lo ignora)
    exefs/   main, main.npdm, rtld, sdk, subsdk0
    romfs/   el RomFS completo, base + 1.9.0
    aoc1/    opcional: RomFS del primer DLC
    aoc2/    opcional: RomFS del segundo DLC (System/AocVersion.txt)
```

Copia o enlace: `mklink /J extracted\romfs D:\BOTW\romfs` ahorra 15 GB. Detalles y DLC:
[docs/game-files.md](../game-files.md). `python tools/sw_probe.py` verifica la versión.

## 4. Compilar

Lo fácil: ejecutá `Install BOTW VR.bat` en la raíz del repo. Pide tus carpetas (ExeFS, RomFS, DLC), las verifica,
instala las herramientas si faltan, las enlaza en `extracted/`, compila y escribe `Play BOTW VR.bat` con las opciones
VR que elijas, más un acceso directo en el escritorio. A mano:


Una vez, en PowerShell en la raíz del repo:

```
powershell -ExecutionPolicy Bypass -File tools\setup_windows.ps1
C:\msys64\usr\bin\pacman -S --needed mingw-w64-ucrt-x86_64-openxr-sdk
```

Después, en Git Bash o MSYS2 UCRT64:

```
export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
make gen          # una vez: recompila el código del juego desde extracted/exefs
make VR=1 -j      # build/savagewind.exe con modo VR
make test-vr      # opcional: tests VR, sin juego ni visor
```

## 5. Jugar en VR (Quest 3 + Virtual Desktop)

1. En el streamer de Virtual Desktop en la PC, runtime OpenXR = **VDXR** (o SteamVR). En el Quest abrí Virtual Desktop
   y conectate a la PC.
2. Ejecutá `tools\vr\play-vr.bat` desde el escritorio de Virtual Desktop (sus variables son la configuración VR,
   docs/VR.md "Settings").
3. Mirá al frente al entrar a jugar: esa es la dirección "adelante" (`vr recenter` en la consola del juego la vuelve a
   fijar). Sin visor o runtime el juego corre plano y `savagewind.log` dice por qué (líneas `[vr]`).

Recomendado en Quest 3: 72 u 80 Hz en Virtual Desktop, `SAVAGEWIND_VR_HEIGHT=1440`. Controles, confort y pendientes:
[docs/VR.md](../VR.md).

El límite es la CPU (el hilo de Savage Wind que traduce los dibujos del juego a Vulkan, ~1,6 µs por dibujo), no la GPU:
cada ojo es un cuadro del juego. Medido en estéreo en la Meseta de los Albores (CPU de escritorio reciente de 16 núcleos, GPU clase RTX 3090; cuadros del
juego por segundo, pares de ojos la mitad): antialiasing TAA 177-190 (~90 pares), SMAA ~198 (~99), sin AA ~216 (~108).
Para 90 Hz usá SMAA o sin antialiasing; a 72 u 80 Hz TAA alcanza.
