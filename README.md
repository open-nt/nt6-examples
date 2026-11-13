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

## Comparing with Qt 6

`qt/` builds `hello_widgets` and `gallery` against Qt 6 from the same sources. It needs Qt 6 and is a
separate project:

```sh
cmake -S qt -B build-qt && cmake --build build-qt
```

## License

MIT; see [LICENSE](LICENSE).
