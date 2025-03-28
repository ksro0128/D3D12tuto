// vertex shader
struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}

// pixel shader
float4 PSMain(PSInput input) : SV_TARGET {
    return float4(input.color, 1.0f);
}
