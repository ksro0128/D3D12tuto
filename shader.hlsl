cbuffer MVPBuffer : register(b0)
{
    matrix model;
    matrix view;
    matrix projection;
};

Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(float4(input.position, 1.0f), model);
    float4 viewPos  = mul(worldPos, view);
    output.position = mul(viewPos, projection);
    output.uv = input.uv;
    output.normal = input.normal;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    float4 texColor = tex.Sample(samp, input.uv);
    return texColor;
}
