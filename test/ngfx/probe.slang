// The module ngfx_slang_sources.goose embeds from a file.
#include <NoGraphicsAPI/shader.slang>
#include "probe_common.slang"
struct Params { float4 tint; float gain; };
GPU_ROOT(Params, root);
[shader("fragment")]
float4 probe() : SV_Target0 { return root.tint * doubled(root.gain); }
