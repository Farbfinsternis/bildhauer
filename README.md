# bildhauer

**bildhauer is a remake of Sculptris**, the free digital sculpting program
that many artists started with.

## Why

Sculptris was loved because it was simple: you push and pull a ball of
virtual clay, and the program adds detail wherever you sculpt. No polygon
counts, no topology, no setup. Then Pixologic bought it and stopped working
on it. Today it is hard to find, no longer maintained, and it may stop
running on future systems.

We think a tool like that should exist and stay free. bildhauer brings
Sculptris back as independent, open software that nobody can buy and shut
down.

## What you can do with it

- **Sculpt like with clay.** Draw, Smooth, Grab, Inflate, Flatten, Pinch,
  Crease, Rotate, Scale, Mask and Reduce, the brushes you know from
  Sculptris. Detail is added only where you work, so you never have to think
  about the mesh.
- **Work symmetrically**, or switch symmetry off for asymmetric details.
- **Shape your strokes with brush masks** (stencil images).
- **Paint your model:** color, bump detail and material layers with matcaps
  (clay, gold and your own images).
- **Open and save Sculptris files (.sc1).** Your old Sculptris models open
  in bildhauer, and files saved by bildhauer open in Sculptris.

## Who it is for

- Beginners who want to try digital sculpting without learning a big program
- Artists who want to rough out a character or an idea quickly
- Everyone who misses Sculptris

## Controls

| | |
|---|---|
| Left mouse | sculpt / paint |
| Ctrl | invert the brush |
| Shift | smooth |
| Right mouse or Alt + left | turn the view |
| Middle mouse | move the view |
| Wheel | zoom |
| `[` `]` | brush size (with Ctrl: strength) |
| Ctrl+Z | undo |

The status bar always shows the keys for the current brush. All shortcuts:
Help > Mouse and keys.

Note: as in Sculptris, switching to paint mode is one way. Once you paint,
the shape of the model is fixed, so save a copy first if you want to keep
sculpting it.

## Status

An early version for Windows. There is no download yet; to try it, build it
from source with the Visual Studio Build Tools:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Developers find the details in [DEVELOPMENT.md](DEVELOPMENT.md).

## License

Free and open under the [MIT License](LICENSE): use it, share it, change it.

Sculptris is a trademark of its owners. bildhauer is an independent project,
not affiliated with Pixologic or Maxon, and contains none of their files.
