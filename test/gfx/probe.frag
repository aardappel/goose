// The shader compiler's own probe (run_tests.py): it is built into every
// compiler, with or without SDL, so this compiles everywhere.
#include "probe_common.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D tex;
layout(set = 3, binding = 0) uniform Params { vec4 tint; float gain; };
void main() { result = texture(tex, v_uv) * tint * doubled(gain); }
