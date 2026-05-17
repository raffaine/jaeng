// vertex.hlsl
[[vk::binding(0, 0)]]
cbuffer PushConstants : register(b0, space0)
{
    uint textureIndex;
    uint samplerIndex;
};

[[vk::binding(5, 0)]]
cbuffer CBFrame : register(b5, space0)
{
#if defined(JAENG_VULKAN)
    row_major float4x4 ViewProj;
#else
    float4x4 ViewProj;
#endif
};
[[vk::binding(6, 0)]]
cbuffer CBObject : register(b6, space0)
{
#if defined(JAENG_VULKAN)
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
#if defined(JAENG_VULKAN)
    o.pos = mul(mul(float4(v.pos, 1.0), World), ViewProj);
#else
    // D3D12 and Metal/Apple use pre-multiplication for column-major matrices
    o.pos = mul(ViewProj, mul(World, float4(v.pos, 1.0)));
#endif
    // Use vertex color so it's not optimized out of the SPIR-V signature
    o.col = float4(v.col, 1.0) * MaterialColor;
    o.uv = v.uv * MaterialUVRect.zw + MaterialUVRect.xy;
    return o;
}
