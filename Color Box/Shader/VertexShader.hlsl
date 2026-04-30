cbuffer cbPerObject : register(b0)
{
    float4x4 gModelViewProj;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float4 Color : COLOR;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
};

VertexOut main(VertexIn vin)
{
    VertexOut vout;
	
    vout.PosH = mul(float4(vin.PosL, 1.0f), gModelViewProj);
    vout.Color = vin.Color;
    
    return vout;
}