cbuffer CBFrame : register(b0, space0)
{
    float4x4 ViewProj;
};

cbuffer CBObject : register(b1, space0)
{
    float4x4 World;
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
    o.pos = mul(ViewProj, mul(World, float4(v.pos, 1.0)));
    o.col = v.col;
    o.uv = v.uv;
    return o;
}
