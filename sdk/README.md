# gx2gl SDK (experimental geometry shader build)

Prebuilt gx2gl (OpenGL 3.3 Core on GX2) and CafeGLSL (runtime GLSL compiler)
for Wii U homebrew, with geometry shader support.

- `include/`: gx2gl headers (`<gl/gl.h>`, `<gx2gl/present.h>`, ...)
- `lib/`: `libgl33_gx2_core.a` and `libcafeglsl.a`
- `template/`: a minimal app with a geometry shader; build it with
  `cmake -S template -B build -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake`
- `docs/geometry-shaders.md`: how to use geometry shaders and what works

Needs devkitPro with the Wii U toolchain (wut) and CMake.

This is an experimental build of work in progress on both projects:
gx2gl is mid-rewrite and not conformant yet, and geometry shader support has
only been run with small shaders. Report problems with the log your app writes.

Licenses: gx2gl is MIT (`licenses/gx2gl.txt`); CafeGLSL is built from Mesa,
whose licenses are in `licenses/cafeglsl/`.
