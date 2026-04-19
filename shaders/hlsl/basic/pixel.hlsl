
#if defined(JAENG_VULKAN)
// Vulkan Bindless path: Set 1 for Textures, Set 2 for Samplers
Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space2);
#define GET_TEXTURE(idx) textures[idx]
#define GET_SAMPLER(idx) samplers[idx]

#elif defined(JAENG_APPLE)
// Metal currently uses discrete arrays (to be evolved to Argument Buffers)
Texture2D textures[128] : register(t0, space0);
SamplerState samplers[16] : register(s0, space0);
#define GET_TEXTURE(idx) textures[idx]
#define GET_SAMPLER(idx) samplers[idx]

#else
// D3D12 Bindless path (SM 6.6+)
#define GET_TEXTURE(idx) ResourceDescriptorHeap[idx]
#define GET_SAMPLER(idx) SamplerDescriptorHeap[idx]
#endif

cbuffer PushConstants : register(b0, space0)
{
    uint textureIndex;
    uint samplerIndex;
};

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
