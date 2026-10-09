// The ngfx layer's downsampling shader, which stdlib/ngfx.goose's
// generate_mips dispatches once per mip level: each texel of a level the
// average of the 2 x 2 above it, read through the texture's sampled view at
// an explicit level and written through the level's storage view. Compiled
// when the layer is built (cmake/ngfx.cmake). Its root is the Goose
// MipRoot; its numthreads is ngfx_draw.cpp's mip_threadgroup.
#include <NoGraphicsAPI/shader.slang>

struct MipRoot
{
    uint source;
    uint target;
    uint level;
    uint width;
    uint height;
    uint source_width;
    uint source_height;
};

GPU_ROOT(MipRoot, root);

[shader("compute")]
[numthreads(8, 8, 1)]
void downsample(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= root.width || id.y >= root.height) return;
    Texture2D<float4> source = gpu_texture<Texture2D<float4>>(root.source);
    RWTexture2D<float4> target = gpu_texture<RWTexture2D<float4>>(root.target);
    uint2 p = id.xy * 2;
    uint2 q = min(p + 1, uint2(root.source_width - 1, root.source_height - 1));
    float4 sum = source.Load(int3(p.x, p.y, root.level)) + source.Load(int3(q.x, p.y, root.level)) +
                 source.Load(int3(p.x, q.y, root.level)) + source.Load(int3(q.x, q.y, root.level));
    target[id.xy] = sum * 0.25;
}
