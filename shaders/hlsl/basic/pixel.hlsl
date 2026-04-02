// pixel.hlsl
Texture2D textures[] : register(t0, space0);
SamplerState samplers[] : register(s0, space0);

cbuffer PushConstants : register(b0, space0)
{
    uint textureIndex;
    uint samplerIndex;
};

struct PSIn {
    float4 pos: SV_Position;
    float3 col: COLOR;
    float2 uv: TEXCOORD;
};

float4 main(PSIn i) : SV_Target {
    float4 tex = textures[textureIndex].Sample(samplers[samplerIndex], i.uv);
    return float4(i.col, 1.0) * tex;
}
