# CFL Tool

A 3DS homebrew app for previewing Mii heads (and bodies) rendered by
[cfl-mii](https://github.com/jamesrhg/cfl-mii).

**cfl-mii is a reimplementation of Nintendo's own CFL Mii-rendering
library, reverse-engineered from a retail 3DS title binary's debug
info and cross-referenced against the real, compiled RFL (Wii) and FFL
(Wii U) Mii libraries - it is not Nintendo's own code, and it is not
guaranteed to be 100% functionally accurate to real CFL.** Most of it
has been validated against real 3DS hardware output, but some corners
(exact pixel-level texture formulas, a handful of edge cases in rarer
Mii data combinations) are still being tracked down. Where this project
found and fixed a real discrepancy from official Mii Maker/Mii Plaza
output, it's noted in cfl-mii's own commit history and code comments -
if something here looks visibly wrong compared to a real Mii Maker
render, it's more likely an unfinished corner of this reimplementation
than an intentional difference.

## Features

- **CharModel Test** - pick one or more Miis from the system Mii
  Selector and view them rendered as full 3D character models, with a
  free-orbiting camera and per-head auto-spin. Several resolution /
  expression-set presets are available depending on how many heads you
  want on screen at once.
- **CharModel + Body Test** - a single Mii's head attached to a real
  Nintendo full-body model (`MaleBody`/`FemaleBody`, scaled per that
  Mii's own build/height), rendered in real time with the same free
  camera as CharModel Test. Toggle the body on/off with X.
- **Icon Test** - renders a Mii through `CFL_CommandMakeModelIcon` and
  displays the resulting icon texture at two sizes, transparent
  background. Both the bodyless icon and an icon with the real
  dedicated `IconBody` asset attached are pre-rendered up front when
  you pick a Mii - X just swaps which one is shown, no re-rendering on
  every press.
- **CharModel from Data** - decodes a sample `CFLStoreData` (the
  standard checksummed Mii exchange format) straight into a rendered
  CharModel, and lets you pick any Mii from the system selector and
  re-encode it back to base64 `CFLStoreData` - a round-trip demo of
  `CFL_MakeStoreData`/`CFL_IsStoreDataValid`.

## Controls

- **D-Pad** - navigate menus / pan the camera
- **A** - confirm
- **B** - back
- **X** - screen-dependent: cycle facial expression (CharModel Test),
  toggle the attached body on/off (CharModel + Body Test, Icon Test)
- **SELECT** - replace the last Mii in the current scene
- **Circle Pad** - look around
- **L / R** - zoom camera in/out
- **START** - exit

## Building

Requires [devkitPro](https://devkitpro.org/) with the 3DS toolchain
(`devkitARM`, `libctru`, `citro3d`, `citro2d`) installed.

```sh
git clone --recurse-submodules <this-repo-url>
cd cfl-tool
make
```

If you already cloned without `--recurse-submodules`:

```sh
git submodule update --init --recursive
```

Produces `cfl-tool.3dsx`.

## Body models

`data/MaleBody.iqm`/`FemaleBody.iqm`/`IconBody.iqm` are real, standard
[IQM (Inter-Quake Model)](http://sauerbraten.org/iqm/) files - the same
open format Blender's own importer/exporter and several other engines
support, not a project-specific format - extracted once, offline, from
real Nintendo body assets, and embedded into the `.3dsx` the same way
the vertex shader is, via devkitPro's own `bin2s` mechanism (see the
`DATA` directory in the `Makefile`) - no extra runtime asset loading or
network access involved. See
[cfl-mii's own README](https://github.com/jamesrhg/cfl-mii#the-iqm-format-in-detail)
for exactly what this format holds and how `CFL_LoadBodyModel` reads
it. cfl-mii itself now supports real skeletal animation playback for a
loaded body model (`CFL_PoseBodyModel`, with a full worked
[example](https://github.com/jamesrhg/cfl-mii#animating-a-body-model)
showing a real anim-only `.iqm` reader and per-frame sampling) - this
app doesn't use it yet, so Body Test still shows these three assets in
their static bind pose only.
