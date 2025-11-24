cbuffer CBTransform : register(b0)
{
    float4x4 uTransform;
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
    o.pos = mul(uTransform, float4(v.pos, 1.0));
    o.col = v.col;
    o.uv = v.uv;
    return o;
}
