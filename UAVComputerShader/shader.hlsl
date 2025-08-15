RWTexture2D<float4> OutputTexture : register(u0);
cbuffer TimeConstants : register(b0)
{
    float Time;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    float2 uv = (float2)id.xy / (float2)800.0f;
    float r = abs(sin(uv.x * 20.0f + Time));
    float g = abs(cos(uv.y * 20.0f - Time));
    float b = sin(uv.x * uv.y * 50.0f + Time * 2.0f);

    OutputTexture[id.xy] = float4(r, g, b, 1.0f);
}