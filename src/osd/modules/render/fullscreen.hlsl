// Fullscreen.hlsl

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };
    o.pos = float4(positions[id], 0.0, 1.0);
    o.uv  = uvs[id];
    return o;
}

Texture2D tex : register(t0);
SamplerState samp : register(s0);

float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
    return tex.Sample(samp, uv);
}
