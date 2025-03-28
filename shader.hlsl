cbuffer MVPBuffer : register(b0)
{
    matrix model;
    matrix view;
    matrix projection;
};

struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(float4(input.position, 1.0f), model);
    float4 viewPos  = mul(worldPos, view);
    output.position = mul(viewPos, projection);

    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    return float4(input.color, 1.0f);
}
