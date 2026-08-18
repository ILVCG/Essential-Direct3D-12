struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    VSOut o;
    float2 p[3] = { float2(-1, 1), float2(3, 1), float2(-1, -3) };
    float2 t[3] = { float2(0, 0), float2(2, 0), float2(0, 2) };
    o.pos = float4(p[vid], 0, 1);
    o.uv = t[vid];
    return o;
}
