# PSYQo backgrounds example

Five textured background scenes, each a half skybox with a few props inside it, lit by a directional light that sweeps across the geometry. It shows how to load a TIM with `psyqo::TIM` and upload it to VRAM coordinates chosen by the application rather than by the file, and how to draw an indexed, textured mesh through the GTE with an ordering table.

The meshes and textures are by [MrQuetch](https://github.com/MrQuetch), and are distributed under the same licence as the rest of psyqo.

## Building

This example builds with xmake only, because the meshes are converted from their `.obj` sources at build time by the `nugget.psxm` rule:

```
xmake f -P . -y
xmake -P . -y
```

The output is `build/psx/mipsel/release/backgrounds.ps-exe`. Defining `BACKGROUNDS_START_SCENE` picks the scene shown at boot, and `BACKGROUNDS_AUTOCYCLE=<frames>` advances to the next one on a timer.

## Controls

L1 and R1 cycle the scenes, the d-pad moves the camera, square and circle move the light by hand, and cross toggles the automatic light sweep.

## Assets

The textures ship as 8bpp TIMs. [`png2tim.py`](png2tim.py) is the one-off tool that produced them from the original PNGs. It copies a palettised PNG through losslessly, and refuses to quantise a truecolour one unless asked to with `--quantise pngquant`.
