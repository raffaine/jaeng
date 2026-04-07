// pixel.hlsl
#ifdef VULKAN
Texture2D textures[] : register(t0, space1);
SamplerState samplers[] : register(s0, space2);
#else
Texture2D textures[] : register(t0, space0);
SamplerState samplers[] : register(s0, space0);
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
    float4 tex = textures[textureIndex].Sample(samplers[samplerIndex], i.uv);
#ifdef VULKAN
    tex = tex.rgba;
#endif
    return i.col * tex;
}
