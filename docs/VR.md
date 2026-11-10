# VR mode (work in progress)

A PC-VR mode for Savage Wind through OpenXR: stereo, 6DOF head tracking, then motion controls. It is opt-in twice:
built only with `make VR=1`, and turned on at run time with `SAVAGEWIND_VR=1`. A build without `VR=1` has none of it.
The full plan, phase by phase with its tests (in Spanish): [VR-PLAN.md](VR-PLAN.md).

## Status

| Phase | What | State |
|---|---|---|
| 0 | Build flag, OpenXR loader, pose / projection math, the `fake_xr` test runtime, CI | done |
| 1 | OpenXR session on the game's Vulkan device; the flat game on a virtual screen | next |
| 2 | Head tracking (one view), the game's projection from the headset's field of view | |
| 3 | Stereo: one game update, two renders | |
| 4 | HUD and menus as layers | |
| 5 | VR controllers as the game's controller | |
| 6+ | Comfort, hands and arms, motion combat, performance | |

Phase 0 only checks that an OpenXR runtime answers and says so in the log; the game is still drawn flat:

```
[vr] OpenXR runtime <name> <version> (phase 0: the game is still drawn flat)
[vr] VR asked for, but no OpenXR runtime answered (xrCreateInstance: -51): staying flat
```

## Building

The loader is the system's: MSYS2 `mingw-w64-ucrt-x86_64-openxr-sdk`, Debian / Ubuntu `libopenxr-dev`. The headers are
in `third_party/openxr/`.

```
make VR=1 -j
SAVAGEWIND_VR=1 build/savagewind.exe extracted --play
```

`OPENXR_LIB` points at another loader (`make VR=1 OPENXR_LIB="-L/path -lopenxr_loader"`).

## Code

| File | What |
|---|---|
| `runtime/vr/vr.h`, `vr.c` | `vr_init` (from `main`, after the settings), `vr_enabled`, `vr_status_text` |
| `runtime/vr/vr_math.h` | Quaternions, poses, the tracking space to game world mapping (`vr_anchor`), recentring, the LookAtCamera eye / at / up of a view, projections from `XrFovf`. Its conventions are at the top of the file |
| `tools/vr/fake_xr/` | `fake_xr`, a scripted OpenXR runtime for tests (below) |
| `tests/vr/`, `tools/vr/test_*.c` | The tests |

## Tests

None of them need game files, a GPU or a headset.

```
make test-vr          # test_vr_math, test_fake_xr_session
make test-vr-loader   # through the system's OpenXR loader: test_fake_xr_loader, test_vr_init (4 runs)
```

`VR_TEST_VERBOSE=1` prints each part of a test as it starts.

- `test_vr_math`: the math against values worked out by hand from the conventions, not from the code (axes, yaw and
  pitch both ways, recentring, every frustum edge of an asymmetric field in each clip-space convention).
- `test_fake_xr_session`: drives `fake_xr` the way the loader and an app do, without the loader: negotiation, the
  session lifecycle and its errors (`SESSION_NOT_READY`, `NOT_STOPPING`, `CALL_ORDER_INVALID`, `FRAME_DISCARDED`,
  `TIME_INVALID` ...), frame timing, scripted head poses, focus changes, an exit the runtime asks for, a lost session.
- `test_fake_xr_loader`: a whole session through the real loader with `XR_RUNTIME_JSON` at `fake_xr`'s manifest.
- `test_vr_init`: `runtime/vr/vr.c` stays off without `SAVAGEWIND_VR`, finds `fake_xr`, and stays flat with a status
  that says why when there is no runtime.

`.github/workflows/vr.yml` runs both targets on Linux and Windows, and builds the whole app with `VR=1` on Windows.

On Windows outside MSYS2, a `libstdc++-6.dll` of another MinGW earlier on `PATH` (Git for Windows has one) keeps a
loader built with a newer GCC from starting (exit code 127): put the compiler's own `bin` first.

### fake_xr

A small OpenXR runtime (`build/vr/fake_xr.dll` / `.so` and `build/vr/fake_xr.json`, `make fake-xr`). Any OpenXR app,
the game included, uses it with `XR_RUNTIME_JSON=build/vr/fake_xr.json`. It follows the session lifecycle strictly, so
an app that gets it wrong fails here as on a real runtime, and its time is deterministic (frame n is shown at a fixed
base plus n periods). What it reads (`tools/vr/fake_xr/fake_xr.h`):

| Variable | |
|---|---|
| `FAKE_XR_SCRIPT` | head poses and events over time: `<t> head x y z yaw pitch roll`, `<t> unfocus`, `focus`, `exit`, `lose` |
| `FAKE_XR_LOG` | one line per state change and per `xrEndFrame` (layer count and kinds) |
| `FAKE_XR_HZ` | display rate (90) |
| `FAKE_XR_IPD` | eye distance in metres (0.063) |

Phase 0 has no graphics in it (sessions need `XR_MND_headless`); phase 1 adds `XR_KHR_vulkan_enable2`, swapchains and a
picture of each eye for the tests that run the game.
