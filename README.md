# CFL Tool

A 3DS homebrew app for previewing Mii heads rendered by
[cfl-mii](https://github.com/jamesrhg/cfl-mii) - a reimplementation of
Nintendo's own CFL Mii-rendering library.

## Features

- **CharModel Test** - pick one or more Miis from the system Mii
  Selector and view them rendered as full 3D character models, with a
  free-orbiting camera and per-head auto-spin. Several resolution /
  expression-set presets are available depending on how many heads you
  want on screen at once.
- **Icon Test** - renders a Mii through `CFL_CommandMakeModelIcon` and
  displays the resulting icon texture at two sizes.
- **CharModel from Data** - decodes a sample `CFLStoreData` (the
  standard checksummed Mii exchange format) straight into a rendered
  CharModel, and lets you pick any Mii from the system selector and
  re-encode it back to base64 `CFLStoreData` - a round-trip demo of
  `CFL_MakeStoreData`/`CFL_IsStoreDataValid`.

## Controls

- **D-Pad** - navigate menus / pan the camera
- **A** - confirm
- **B** - back
- **X** - cycle facial expression
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

Produces `cfl-mii-demo.3dsx`.
