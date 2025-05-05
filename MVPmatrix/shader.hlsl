
struct MVPMatrix
{
	matrix m;
};

ConstantBuffer<MVPMatrix> mvp1 : register(b0);
ConstantBuffer<MVPMatrix> mvp2 : register(b1);
ConstantBuffer<MVPMatrix> mvp3 : register(b2);

struct VSInput {
    float3 pos : POSITION;
    float3 col : COLOR;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float3 col : COLOR;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = mul(mvp3.m, float4(input.pos, 1.0));
    output.col = input.col;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return float4(input.col, 1.0);
}
