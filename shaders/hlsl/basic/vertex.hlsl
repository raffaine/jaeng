// vertex.hlsl
cbuffer PushConstants : register(b0, space0)
{
    uint textureIndex;
    uint samplerIndex;
};

cbuffer CBFrame : register(b1, space0)
{
#ifdef VULKAN
    row_major float4x4 ViewProj;
#else
    float4x4 ViewProj;
#endif
};

cbuffer CBObject : register(b2, space0)
{
#ifdef VULKAN
    row_major float4x4 World;
#else
    float4x4 World;
#endif
};

struct VSIn {
    float3 pos: POSITION;
    float3 col: COLOR;
    float2 uv: TEXCOORD;
};

struct VSOut {
    float4 pos: SV_Position;
    float3 col: COLOR;
    float2 uv: TEXCOORD;
};

VSOut main(VSIn v) {
    VSOut o;
#ifdef VULKAN
    o.pos = mul(mul(float4(v.pos, 1.0), World), ViewProj);
    o.pos.y = -o.pos.y;
#else
    o.pos = mul(ViewProj, mul(World, float4(v.pos, 1.0)));
#endif
    o.col = v.col;
    o.uv = v.uv;
    return o;
}
