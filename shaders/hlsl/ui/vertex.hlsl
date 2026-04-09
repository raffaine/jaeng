// vertex.hlsl
cbuffer PushConstants : register(b0, space0)
{
    uint textureIndex;
    uint samplerIndex;
};

cbuffer CBFrame : register(b1, space0)
{
#ifdef JAENG_VULKAN
    row_major float4x4 ViewProj;
#else
    float4x4 ViewProj;
#endif
};
cbuffer CBObject : register(b2, space0)
{
#ifdef JAENG_VULKAN
    row_major float4x4 World;
    float4 MaterialColor;
    float4 MaterialUVRect;
#else
    float4x4 World;
    float4 MaterialColor;
    float4 MaterialUVRect;
#endif
};

struct VSIn {
    float3 pos: POSITION;
    float3 col: COLOR;
    float2 uv: TEXCOORD;
};

struct VSOut {
    float4 pos: SV_Position;
    float4 col: COLOR;
    float2 uv: TEXCOORD;
};

VSOut main(VSIn v) {
    VSOut o;
#ifdef JAENG_VULKAN
    o.pos = mul(mul(float4(v.pos, 1.0), World), ViewProj);
#else
    o.pos = mul(ViewProj, mul(World, float4(v.pos, 1.0)));
#endif
    // Ignore vertex colors for UI, use white
    o.col = float4(1.0, 1.0, 1.0, 1.0) * MaterialColor;
    o.uv = v.uv * MaterialUVRect.zw + MaterialUVRect.xy;
    return o;
}
