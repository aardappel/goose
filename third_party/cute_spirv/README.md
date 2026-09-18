# cute_spirv

`cute_spirv.h` (v1.09) and its dependency `ckit.h`, copied unmodified from
[RandyGaul/cute_framework](https://github.com/RandyGaul/cute_framework),
`libraries/cute/`, at commit `d7db751c67e723aea0724571ed22705641f06eac`
(2026-09-15).

`cute_spirv.h` compiles a subset of GLSL 450 to SPIR-V and transpiles it to
HLSL (Shader Model 5.1) and MSL, following SDL_GPU's binding conventions. The
compiler uses it to implement `embed_shader`; `src/shaderc.c` is the one
translation unit that compiles both headers.

Licenses:

* `cute_spirv.h`: zlib, or public domain (Unlicense), at your choice; the full
  text is at the end of the file.
* `ckit.h`: public domain.

To update, copy both files from a newer cute_framework commit and record that
commit here.
