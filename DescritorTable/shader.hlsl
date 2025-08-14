
struct MVPMatrix
{
	matrix m;
};

ConstantBuffer<MVPMatrix> mvp : register(b0);
Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

struct VSInput {
    float3 pos : POSITION;
	float2 uv : TEXCOORD;
};

struct PSInput {
    float4 pos : SV_POSITION;
	float2 uv : TEXCOORD;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.pos = mul(mvp.m, float4(input.pos, 1.0));
    output.uv = input.uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
	return tex.Sample(samplerState, input.uv);
}
