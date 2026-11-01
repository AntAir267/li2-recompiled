Third-party code.

Committed (vendored):
- dmusic/: GothicKit dmusic (MIT, dmusic/LICENSE.md) with TinySoundFont (MIT), patched for LEGO Island 2's
  music (sequence tracks, DLS lookup by GUID,
  groove fixes; changes marked "li2-rip" in the source).
- dr_libs/dr_mp3.h: David Reid's MP3 decoder (public domain or MIT-0, at the end of the file).
- pl_mpeg/pl_mpeg.h: Dominic Szablewski's MPEG-1 decoder (MIT, at the top of the file).

Built locally, not committed:
- SDL3 (zlib licence), 3.4.18 or later: the official source release from
  github.com/libsdl-org/SDL, unpacked here as SDL3-<version>/ and installed into sdl3/ as a static
  library with its headers. From this folder:

      cmake -S SDL3-3.4.18 -B SDL3-build -DSDL_SHARED=OFF -DSDL_STATIC=ON \
            -DSDL_X11_XSCRNSAVER=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/sdl3"
      cmake --build SDL3-build -j8 && cmake --install SDL3-build

  (or point the Makefile at another copy: `make SDL3=/path/to/sdl3`). If configuring stops on a
  missing dependency, see https://wiki.libsdl.org/SDL3/README-linux#build-dependencies.
