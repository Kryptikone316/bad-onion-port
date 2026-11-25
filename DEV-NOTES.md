# Bad Onion (SF3500) - dev notes

* The game is Fenix 0.80 bytecode (DCB header version 0). Fenix 0.92 and BennuGD refuse it, and there is no
  `.prg` to recompile, so the interpreter has to be Fenix 0.80 itself. Source: Fenix 0.80 full release (`fenix080/`).
* Changes to the Fenix source: `fxi/src/img_png.c` uses `png_jmpbuf()` (modern libpng). Nothing else. Windows-only
  includes are covered by `shim/direct.h`. Mikmod (Fenix's own tracker code) is not built; music goes through
  SDL2_mixer + libxmp-lite.
* `fnc_exports.c` is built so the `-rdynamic` DLL mechanism links, though the game uses no DLLs.
* The game sets a 320x240 8-bit mode and is started with `-b` (double buffer): Fenix then draws straight into
  `screen->pixels`, and a flip is one palette conversion (76,800 table lookups) plus the driver call.
* In the game, Esc returns to the title screen and Esc at the title leaves, with exit code 0. (The earlier
  armhf/SDL1.2-compat attempt crashed on exit with `free(): invalid pointer`; this build does not.)
* Pad map (`jk_key` in `shim/fxsdl.c`): D-pad arrows, A and B Space, X and Start Enter, Select Esc.
* Frame rate: the shim logs `fxsdl: perf frames=... avg=... present=...` every 250 frames to stderr
  (`log.txt` on the card). Under qemu the game runs at about 22 fps; Fenix paces itself to 25.
* The shim keeps SDL 1.2 and SDL2 apart on purpose: `SDL.h` renames every SDL 1.2 function with a macro, so
  nothing collides with the real SDL2 symbols linked for the audio side.
* `SDL_keysym.h` and `SDL_types.h` are the SDL 1.2 headers from the Fenix tree (LGPL).
* Reuses the Altitude port's SDL2 patch (audio pacing for the dummy device, divide-by-zero guard) and its
  SDL2_mixer + libxmp-lite patch. libmodplug faults on unaligned access on this CPU; libxmp-lite does not.
* **Mixer channels must be 8 or fewer.** fxi's `sound.c` keeps `lista_canales[8]`, indexed by the channel
  SDL_mixer picks; the first build allocated 16. In that binary the stray writes would have landed in padding and
  in two variables the game never uses (pathfinding block size, DLL export table), and needed 9 or more sounds at
  once, so no fault was seen, but it depended on the memory layout.
