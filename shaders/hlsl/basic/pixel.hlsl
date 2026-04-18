// pixel.hlsl
#ifdef JAENG_VULKAN
Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space2);
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
#ifdef JAENG_VULKAN
    float4 tex = textures[textureIndex].Sample(samplers[samplerIndex], i.uv);
    tex = tex.rgba;
#else
    Texture2D texResource = ResourceDescriptorHeap[textureIndex];
    SamplerState smpResource = SamplerDescriptorHeap[samplerIndex];
    float4 tex = texResource.Sample(smpResource, i.uv);
#endif
    return i.col * tex;
}
