# cute_spirv

`cute_spirv.h` (v1.11) and its dependency `ckit.h`, copied unmodified from
[RandyGaul/cute_framework](https://github.com/RandyGaul/cute_framework),
`libraries/cute/`, at commit `a5c53e4a7ce9ccda35ae28a1e773880b2d70ca0d`
(2026-10-07).

`cute_spirv.h` compiles a subset of GLSL 450 to SPIR-V and transpiles it to
HLSL (Shader Model 5.1) and MSL, following SDL_GPU's binding conventions. The
compiler uses it to implement `embed_shader`. Both headers are compiled in
`src/shaderc.c`.

Licenses:

* `cute_spirv.h`: choose the zlib license or the public-domain dedication
  (Unlicense). The full text is at the end of the file.
* `ckit.h`: public domain.

To update, copy both files from a newer cute_framework commit and record that
commit here.
