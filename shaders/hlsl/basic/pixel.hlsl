Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

struct PSIn {
    float4 pos: SV_Position;
    float3 col: COLOR;
    float2 uv: TEXCOORD;
};

float4 main(PSIn i) : SV_Target {
    float4 tex = t0.Sample(s0, i.uv);
    return float4(i.col, 1.0) * tex;
}
