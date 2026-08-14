# CFL Tool

A 3DS homebrew app for previewing Mii heads (and full-body Miis)
rendered by [cfl-mii](https://github.com/jamesrhg/cfl-mii).

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

**cfl-mii itself only ever builds and renders a Mii *head*.** Real CFL
has no body-model concept anywhere (confirmed via the decompile), so
this app owns everything below the neck entirely on its own side -
real, standard [IQM](http://sauerbraten.org/iqm/) parsing, per-vertex
linear-blend skinning, per-Mii build/height scaling, and skeletal
animation sampling are all plain application code in `source/main.c`,
built against nothing but cfl-mii's own public API
(`CFLCharModel`/`CFLPart`, `CFL_GetShaderLocations`,
`CFL_BindDefaultShader`/`CFL_SetDefaultMaterial`). See
[Body models](#body-models) below for the full picture.

## Features

- **CharModel Test** - pick one or more Miis from the system Mii
  Selector and view them rendered as full 3D character models, with a
  free-orbiting camera and per-head auto-spin. Several resolution /
  expression-set presets are available depending on how many heads you
  want on screen at once.
- **CharModel + Body Test** - a single Mii's head attached to a
  full-body model, rendered in real time with the same free camera as
  CharModel Test. Two real body sources, swapped with Y: the real
  Nintendo Mii Maker bodies (`MaleBody`/`FemaleBody`, scaled per that
  Mii's own real build/height) and the real StreetPass Mii Plaza bodies
  (`StreetPassBodyMale`/`StreetPassBodyFemale`, fixed size - matching
  how the real StreetPass Mii Plaza game itself displayed them).
- **Icon Test** - renders a Mii through cfl-mii's own real
  `CFL_CommandMakeModelIcon` (head only) and displays the resulting
  icon texture at two sizes, transparent background. X toggles to a
  second, body-enabled pair rendered by this app's own
  `appMakeModelIconWithBody` - a from-scratch reimplementation of
  `CFL_CommandMakeModelIcon`'s own real camera/render-target/depth
  setup that also draws a real `IconBody` alongside the head, entirely
  on this app's own side (cfl-mii itself has no body-attached icon path
  at all). Both pairs are pre-rendered up front when you pick a Mii - X
  just swaps which one is shown, no re-rendering on every press. The
  on-screen label always names whichever real function actually
  produced the icon currently displayed.
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
  toggle the attached body icon pair (Icon Test), pause/resume the body
  animation when one is active (CharModel + Body Test)
- **Y** - CharModel + Body Test only: swap between the Mii Maker and
  StreetPass body sources
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

All six of `data/*.iqm` (`MaleBody`, `FemaleBody`, `IconBody`,
`StreetPassBodyMale`, `StreetPassBodyFemale`, `DefaultAnim`) are real,
standard [IQM (Inter-Quake Model)](http://sauerbraten.org/iqm/) files -
the same open format Blender's own importer/exporter and several other
engines support, not a project-specific format - extracted once,
offline, from real Nintendo body assets, and embedded into the
`.3dsx` the same way the vertex shader is, via devkitPro's own `bin2o`
mechanism (see the `DATA` directory in the `Makefile`) - no extra
runtime asset loading or network access involved.

**Every piece of body handling lives entirely in this app**, not in
cfl-mii: the real IQM parser (joints, meshes, linear-blend skinning
vertex arrays), per-Mii build/height scaling
(`nn::mii::detail::GetBodyScale`, applied only to the Mii Maker bodies -
the StreetPass bodies render at a fixed size, matching how the real
StreetPass Mii Plaza game itself used them), and a from-scratch
anim-only `.iqm` reader/sampler for `DefaultAnim.iqm` are all plain C
in `source/main.c`. This is a deliberate architecture choice, not a
missing feature in cfl-mii - see the note at the top of this README and
[cfl-mii's own README](https://github.com/jamesrhg/cfl-mii) for why.

**Body animation playback exists but is currently disabled.**
`DefaultAnim.iqm` was authored against the female Mii Maker body's own
bind pose - replaying it unmodified on the male body's differently-
proportioned skeleton visibly misaligns the neck (no per-body
retargeting is implemented yet). Body Test always shows the plain bind
pose (T-pose) for both bodies until a real, per-body-retargeted clip
(or a body-proportion-independent one) exists - the sampling/posing
code itself (`sampleBodyAnimFrame`/`poseBodyModel`) is intact and
already used once this is revisited.

**The StreetPass bodies' own real pants/skin split** is a texture-based
mask in the original asset (a real, plain black/white palette texture,
never shipped alongside the model), not a second mesh the way Mii Maker's
`MaleBody`/`FemaleBody` use - `parseBodyIqm`'s own
`splitBottomCircleFromBody` reconstructs the real boundary geometrically
(position + radius) instead, calibrated against a real independent rip
of the same asset that *does* include the original texture and UV data.
