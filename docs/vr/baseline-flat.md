# Flat baseline (before any VR rendering)

`tools/perf_routes.py run --size 1920x1080 --runs 1`, build `VR=1` with VR off at run time (`SAVAGEWIND_VR` unset), commit 5b80a83 + phase-0 CI fix. Game 1.9.0, no DLC. Windows 11, RTX 3090 (GPU 0), NVIDIA driver 616.92 (Vulkan 1.4.351), 2560x1440 desktop, game window 1920x1080, frame cap off, vsync off. The `rito` route failed in this run (`look: No camera`).

| config | size | route | runs | fps avg (min-max) | 1% low | p99 ms | max ms | main busy ms | render busy ms | GPU ms | main waits render/GPU/other | CPU ms/frame | compiles | limits | top GPU passes (last run) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| flat-vr1build | 1920x1080 | plateau | 1 | 192.2 (192.2-192.2) | 134.3 | 7.4 | 12.9 | 3.34 | 5.09 | 3.56 | 22/3/10% | 18.8 | 0 | render 98% | 0.41 ms dispatch 1x1x1 (local 32x1x1); 0.26 ms pass: 6 color 1600x900 fmt 0x25, depth 1600x900, first fs 5ed52f953d4f83dc (3265 draws); 0.24 ms dispatch 4x1x1 (local 32x1x1) |
| flat-vr1build | 1920x1080 | kakariko | 1 | 201.6 (201.6-201.6) | 162.8 | 6.1 | 11.2 | 3.23 | 4.86 | 3.52 | 4/18/13% | 18.2 | 0 | render 98% | 0.47 ms dispatch 1x1x1 (local 32x1x1); 0.23 ms pass: 6 color 1600x900 fmt 0x25, depth 1600x900, first fs 5ed52f953d4f83dc (1337 draws); 0.22 ms dispatch 16x1x1 (local 32x1x1) |
| flat-vr1build | 1920x1080 | field | 1 | 229.2 (229.2-229.2) | 186.8 | 5.3 | 11.6 | 3.06 | 4.25 | 3.06 | 0/16/13% | 16.1 | 10 | render 97% | 0.39 ms dispatch 1x1x1 (local 32x1x1); 0.26 ms dispatch 16x1x1 (local 32x1x1); 0.20 ms pass: 1 color 448x448 fmt 0x6, depth 0x0, first fs 74206931e75327e8 (3 draws) |
| flat-vr1build | 1920x1080 | castle | 1 | 166.5 (166.5-166.5) | 141.5 | 7.1 | 10.5 | 3.51 | 5.96 | 4.55 | 6/27/9% | 18.7 | 0 | render 99% | 1.02 ms dispatch 1x1x1 (local 32x1x1); 0.40 ms dispatch 3x1x1 (local 32x1x1); 0.29 ms pass: 1 color 64x64 fmt 0x3f, depth 64x64, first fs 55ea395ba523a189 (321 draws) |
| flat-vr1build | 1920x1080 | hateno | 1 | 209.2 (209.2-209.2) | 172.1 | 5.8 | 12.0 | 3.19 | 4.63 | 3.33 | 13/11/9% | 17.9 | 3 | render 97% | 0.43 ms dispatch 1x1x1 (local 32x1x1); 0.25 ms dispatch 16x1x1 (local 32x1x1); 0.21 ms pass: 6 color 1600x900 fmt 0x25, depth 1600x900, first fs 5ed52f953d4f83dc (1658 draws) |
| flat-vr1build | 1920x1080 | korok | 1 | 168.8 (168.8-168.8) | 144.1 | 6.9 | 9.9 | 3.14 | 5.86 | 4.48 | 1/39/7% | 16.5 | 0 | render 99% | 1.39 ms dispatch 3x1x1 (local 32x1x1); 0.28 ms dispatch 16x1x1 (local 32x1x1); 0.24 ms dispatch 1x1x1 (local 32x1x1) |

| config | size | route | RAM peak / median MB | GPU local peak MB | GPU host peak MB | system used peak MB | tex ASTC native MB | tex ASTC->RGBA8 MB (ASTC size) | tex BC MB | tex RT MB | tex other MB | ASTC decode ms / window |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| flat-vr1build | 1920x1080 | plateau | 4340 / 4340 | 3785 | 3184 | 33415 | 0 | 0 (0) | 194 | 174 | 61 | 0.0 |
| flat-vr1build | 1920x1080 | kakariko | 4250 / 4250 | 4155 | 3186 | 32867 | 0 | 0 (0) | 135 | 313 | 70 | 0.0 |
| flat-vr1build | 1920x1080 | field | 4335 / 4320 | 4153 | 3252 | 32861 | 0 | 0 (0) | 172 | 321 | 72 | 0.0 |
| flat-vr1build | 1920x1080 | castle | 4509 / 4509 | 4043 | 3252 | 33034 | 0 | 0 (0) | 212 | 174 | 63 | 0.0 |
| flat-vr1build | 1920x1080 | hateno | 4481 / 4481 | 4152 | 3252 | 33013 | 0 | 0 (0) | 159 | 314 | 77 | 0.0 |
| flat-vr1build | 1920x1080 | korok | 4358 / 4357 | 4043 | 3252 | 32916 | 0 | 0 (0) | 142 | 172 | 63 | 0.0 |

| config | size | route | runs | median frame ms | hitches (> 2.0x median) / run | hitch ms / run | causes (all runs) | builds / window | build waits ms | uploads MB | file reads MB |
|---|---|---|---|---|---|---|---|---|---|---|---|
| flat-vr1build | 1920x1080 | plateau | 1 | 4.99 | 0.0 | 0 | none | 0 | 0.0 | 4727 | 0.0 |
| flat-vr1build | 1920x1080 | kakariko | 1 | 4.85 | 0.0 | 0 | none | 0 | 0.0 | 4745 | 1.1 |
| flat-vr1build | 1920x1080 | field | 1 | 4.27 | 0.0 | 0 | none | 12 | 0.0 | 3799 | 4.7 |
| flat-vr1build | 1920x1080 | castle | 1 | 5.90 | 0.0 | 0 | none | 0 | 0.0 | 5180 | 1.1 |
| flat-vr1build | 1920x1080 | hateno | 1 | 4.69 | 0.0 | 0 | none | 4 | 0.0 | 5078 | 1.2 |
| flat-vr1build | 1920x1080 | korok | 1 | 5.84 | 0.0 | 0 | none | 0 | 0.0 | 3441 | 1.2 |
