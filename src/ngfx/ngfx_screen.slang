// The ngfx layer's own shader: copying the screen into the window's image at
// the end of a frame, a fullscreen triangle sampling the screen. Compiled
// when the layer is built (cmake/ngfx.cmake), with NoGraphicsAPI's options.
#include <NoGraphicsAPI/shader.slang>

struct CopyRoot
{
    uint texture;
    uint sampler;
};

GPU_ROOT(CopyRoot, root);

struct CopyVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Vertices 0, 1, 2 at uv (0, 0), (2, 0), (0, 2): the screen's top left is
// clip (-1, -1), as NoGraphicsAPI's clip space has y down.
[shader("vertex")]
CopyVertex copyVertex(uint vertex_id : SV_VertexID)
{
    CopyVertex output;
    output.uv = float2(float((vertex_id << 1) & 2), float(vertex_id & 2));
    output.position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 copyFragment(CopyVertex input) : SV_Target0
{
    Texture2D<float4> screen = gpu_texture<Texture2D<float4>>(root.texture);
    return screen.Sample(gpu_sampler(root.sampler), input.uv);
}
