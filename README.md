<p align="center">
  <img src="widgets/nt-logo.svg" alt="Nt logo" width="128" height="128">
</p>

# nt6-examples

[Nt on GitHub](https://github.com/open-nt) ·
[Examples repository](https://github.com/open-nt/nt6-examples) ·
[Snap package](https://snapcraft.io/open-nt6) ·
[MIT License](LICENSE)

C++ example applications for Nt 6, a small MIT-licensed, Qt 6 compatible implementation of a subset of the
Qt 6 API: widgets, signals and slots, `moc`, SVG, WebSockets, an HTTP server, D-Bus and serial port.
The sources are the ones in the `examples/` directory of Nt, set up to build with CMake against an installed Nt.

## Building

Install Nt, for instance from the snap, and the packages its static libraries link against:

```sh
sudo snap install open-nt6
sudo apt install cmake g++ libssl-dev libfreetype-dev libfontconfig-dev libxcb1-dev
```

Then build and test everything:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The snap's location, `/snap/open-nt6/current/usr`, is searched by default. For an Nt that is installed
elsewhere, pass `-DCMAKE_PREFIX_PATH=<prefix>`.

`hello/`, `signals/` and `widgets/` are also projects of their own, for example
`cmake -S hello -B build-hello && cmake --build build-hello`.

## Examples

| Directory     | Program                                      | Shows                                                         |
|---------------|----------------------------------------------|---------------------------------------------------------------|
| `hello/`      | `hello`                                      | A `QCoreApplication` and a timer                              |
| `signals/`    | `signals`                                    | The signal and slot patterns of the "Signals and Slots" guide |
| `moc/`        | `moc_example`                                | `Q_OBJECT` classes processed by `ntmoc`, as with automoc      |
| `test/`       | `tst_counter`                                | A test written with Nt Test                                   |
| `gui/`        | `hello_window`                               | A window painted with `QPainter`                              |
| `widgets/`    | `hello_widgets`, `gallery`, `render_widgets` | A form, a tour of the widgets, and widgets rendered to PNG    |
| `svg/`        | `svg_viewer`                                 | Showing and converting an SVG file                            |
| `serialport/` | `serialterm`                                 | A serial terminal                                             |
| `websockets/` | `websocket_echo`                             | A WebSocket echo server and client                            |
| `httpserver/` | `hello_server`                               | A small web service                                           |
| `dbus/`       | `dbus_clock`                                 | A clock on the D-Bus session bus                              |
| `platformer/` | `mochimaru`, `sprite_sheet`                  | A platform game: Nt Gamepad, Nt FM sound, a MIDI soundtrack and an OpenGL window |
| `raytracing/` | `pathtracer`                                 | A path tracer on the Vulkan ray tracing pipeline in a `QVulkanWindow`, with the RTX extensions |

Without a display, run the graphical examples with `-platform offscreen`.

## Mochimaru no Daibouken

`platformer/` is a small, cute platform game. Run `build/mochimaru`, or `build/mochimaru song.mid` (several
files, or a directory of them, work too) to play with your own MIDI soundtrack. The sound effects and the
soundtrack come from the FM synthesizer of Nt FM; the controls come from Nt Gamepad.

| Action        | Gamepad                | Keyboard          |
|---------------|------------------------|-------------------|
| Move          | D-pad or left stick    | Arrows or WASD    |
| Jump          | A                      | Space, Z or Up    |
| Run           | X or B                 | X or Shift        |
| Pause / start | Start                  | Esc or Enter      |
| Music on/off  | Select                 | M                 |
| Next song     | Y                      | N                 |
| Fullscreen    |                        | Alt+Enter or F11  |
| Quit          |                        | Q on the pause screen |

The pixel art is text, in `platformer/sprites.h`; `sprite_sheet` draws all of it on one sheet for review.
`mochimaru --export-song tune.mid` writes the built-in tune, as a starting point for your own.

## Path tracer

<p align="center"><img src="raytracing/pathtracer.png" alt="The path tracer: spheres of glass, metal and color on a checkered floor" width="640"></p>

`raytracing/` renders a scene of spheres and boxes with a progressive path tracer that runs on the ray tracing
pipeline of Vulkan (`VK_KHR_ray_tracing_pipeline`) in a `QVulkanWindow`: several paths per pixel per frame, as many
as keep about 90 frames per second, up to 16 bounces, the sun and the lamps sampled directly with shadow rays, and the
frames accumulated while the scene stands still. Run `build/pathtracer`, or `build/pathtracer --fly` to let the camera
circle the scene; `pathtracer --devices` lists the GPUs and `--device N` picks one (by default the first discrete GPU
with ray tracing), `--spp N` fixes the paths per pixel, `--vsync` holds the frames to the display's rate, `--still`
starts with the scene at rest so the image converges, `--screenshot out.png` saves the picture when `--frames N` are
done, and `--validate` enables the validation layer. It needs a GPU with ray tracing (NVIDIA RTX,
AMD RDNA 2 or later, Intel Arc), or Mesa's `lavapipe` for a slow software run, and an Nt built with the Vulkan
headers. On a GeForce RTX 5070 Ti Laptop GPU it traces about 1.7 billion paths per second at 2560x1440 with 6
bounces, and the picture above converged in a few seconds.

Beyond the core extensions it shows what an RTX GPU adds:

- **Shader execution reordering** (`VK_NV_ray_tracing_invocation_reorder`, Ada and Blackwell): the threads of a wave
  are regrouped by the material they hit before the hit shaders run. Press R to switch it on and off and watch the
  frame rate in the title; the picture does not change.
- **Ray tracing position fetch** (`VK_KHR_ray_tracing_position_fetch`): the closest-hit shader reads the vertices of
  the triangle it hit from the acceleration structure, so the meshes bind no vertex buffers.
- **Procedural geometry**: the spheres are analytic, intersected by an intersection shader, and perfectly round at any
  distance; the boxes and the floor are triangles.
- **A top level rebuilt every frame** while the scene moves, and shadow rays nested in the hit shaders for the
  direct light.

The window title shows the GPU, the resolution, the frame rate, the paths per pixel and per second, the bounces, the
reordering state, and the sample count once the image converges.

| Action                | Control                      |
|-----------------------|------------------------------|
| Orbit the camera      | Drag with the left button    |
| Zoom                  | Mouse wheel                  |
| Pause / resume motion | Space (paused, the image converges) |
| Fly around            | F, or start with `--fly`     |
| Reordering on/off     | R                            |
| More / fewer bounces  | + / -                        |
| Screenshot (PNG)      | S                            |
| Quit                  | Q or Esc                     |

## Comparing with Qt 6

`qt/` builds `hello_widgets` and `gallery` against Qt 6 from the same sources. It needs Qt 6 and is a
separate project:

```sh
cmake -S qt -B build-qt && cmake --build build-qt
```

## License

MIT; see [LICENSE](LICENSE).
