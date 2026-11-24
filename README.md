# Bad Onion for the Datafrog SF3500

*Bad Onion* (悪いタマネギ) is a 2003 freeware platformer by Outhouse Software, made with the Fenix game
engine. This repository ports it to the Datafrog SF3500 handheld (MIPS32, no GPU, Linux, display driver and
gamepad supplied by the TreeFrogUI front-end).

The game's files are the author's, unchanged (`game/`). Only the interpreter is built here: the game ships as
compiled Fenix 0.80 bytecode with no script source, which the later Fenix and BennuGD interpreters cannot load,
so the original **Fenix 0.80** interpreter (`fenix080/`, GPL) is built for the SF3500 instead.

## How it works

Fenix 0.80 is written against SDL 1.2. The SF3500 has no SDL video or input device, so `shim/SDL.h` declares
the small part of the SDL 1.2 API that Fenix uses, with every function renamed (`SDL_Init` is `fxsdl_Init`), and
`shim/fxsdl.c` implements it directly:

* **video**: Fenix draws into an 8-bit surface owned by the shim; each flip converts it through the palette to
  RGB565 and hands it to the display driver (`sf3500/hwdisp.c`). `SF3500_ASPECT=16:9` pads the 4:3 picture.
* **input**: the pad's shared memory (`sf3500/joykey.c`) becomes SDL 1.2 key state and key events. Select + Start quits.
* **timing**: `SDL_GetTicks` / `SDL_Delay` on the monotonic clock.
* **audio** (`shim/fxsdl_audio.c`): real SDL2 and SDL2_mixer (libxmp-lite for the XM songs) with SDL's dummy audio
  device for pacing; the mixed output is tapped with `Mix_SetPostMix()` and written to the device's audio ring
  (`sf3500/hwaudio.c`) at its native 48000 Hz.

## Build

You need the SF3500 MIPS toolchain, cmake, make, and three upstream tarballs in `deps/` (checksums are verified):

| File | From |
|---|---|
| `SDL2-2.0.14.tar.gz` | https://github.com/libsdl-org/SDL/releases/download/release-2.0.14/SDL2-2.0.14.tar.gz |
| `SDL2_mixer-2.6.3.tar.gz` | https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.6.3/SDL2_mixer-2.6.3.tar.gz |
| `libxmp-lite-4.6.0.tar.gz` | https://github.com/libxmp/libxmp/releases/download/libxmp-4.6.0/libxmp-lite-4.6.0.tar.gz |

```sh
SF3500_TC=/path/to/toolchain/host ./build_sf3500.sh   # build-sf3500/fxi.mipsel
./package.sh                                          # package/ for the card's roms/ports
```

On the device the game runs as `fxi.mipsel -b badonion.dcb`. The executable's name must start with `fxi`.

## Testing without the device

`tools/qrun.sh <folder> -b badonion.dcb` runs it under qemu-user. Hooks (no cost unless set):
`SF3500_KEYSCRIPT="150:E,400:R:300"` (frame:key[:frames]; E enter, S space, L R U D arrows, C escape),
`SF3500_SHOT_PREFIX=p SF3500_SHOT_FRAMES=100,200` (writes `p_100.ppm`...), `SF3500_QUIT_FRAME=n`.

## Licences

The game is the author's freeware. The Fenix interpreter and the shim are GPL version 2 or later; the glue in
`sf3500/` is derived from TreeFrogUI's SF3000 code; scripts and packaging are under the Unlicense. See
`licenses/CREDITS.txt`.
