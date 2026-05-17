
#if defined(JAENG_VULKAN) || defined(JAENG_APPLE)
// Bindless path: Set 1 for Textures, Set 2 for Samplers
[[vk::binding(0, 1)]] Texture2D textures[4096] : register(t0, space1);
[[vk::binding(0, 2)]] SamplerState samplers[256] : register(s0, space2);
#define GET_TEXTURE(idx) textures[idx]
#define GET_SAMPLER(idx) samplers[idx]

#elif !defined(__directx_shader_model)
// Discrete path for Vulkan fallback or Metal (via SPIR-V transpilation)
Texture2D textures[128] : register(t0, space0);
SamplerState samplers[16] : register(s0, space0);
#define GET_TEXTURE(idx) textures[idx]
#define GET_SAMPLER(idx) samplers[idx]

#else
// D3D12 Bindless path (SM 6.6+)
#define GET_TEXTURE(idx) ResourceDescriptorHeap[idx]
#define GET_SAMPLER(idx) SamplerDescriptorHeap[idx]
#endif

#if defined(JAENG_VULKAN) || defined(JAENG_APPLE)
[[vk::binding(0, 0)]]
cbuffer PushConstants
{
    uint textureIndex;
    uint samplerIndex;
};
#else
cbuffer PushConstants : register(b0, space0)
{
    uint textureIndex;
    uint samplerIndex;
};
#endif

struct PSIn {
    float4 pos: SV_Position;
    float4 col: COLOR;
    float2 uv: TEXCOORD;
};

float4 main(PSIn i) : SV_Target {
    Texture2D tex = GET_TEXTURE(textureIndex);
    SamplerState samp = GET_SAMPLER(samplerIndex);
    return i.col * tex.Sample(samp, i.uv);
}
