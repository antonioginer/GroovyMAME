// FullscreenShaders.hlsl

// -------------------------------------
// Vertex Shader
// -------------------------------------
struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VS_Main(uint id : SV_VertexID)
{
    VSOut o;

    // Fullscreen triangle
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

// -------------------------------------
// Common declarations
// -------------------------------------
Texture2D tex : register(t0);
SamplerState samp : register(s0);

// -------------------------------------
// Pixel Shader 0: Escalado fraccional X e Y (sin interpolar)
// -------------------------------------
float4 PS_Default(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
//    return tex.Sample(samp, uv);
    // Obtener dimensiones de la textura
    uint texW_u, texH_u;
    tex.GetDimensions(texW_u, texH_u);
    int texW = int(texW_u);
    int texH = int(texH_u);

    // Posición en espacio de texel
    float2 texPos = uv * float2(texW-1, texH-1);

    // Texel más cercano
    float xCenter = floor(texPos.x) + 0.5;
    float yCenter = floor(texPos.y) + 0.5;

    // Distancia al centro del texel
    float dx = texPos.x - xCenter;
    float dy = texPos.y - yCenter;

    // Posiciones de los texels vecinos

    int x0 = int(floor(texPos.x));
    int y0 = int(floor(texPos.y));
    int x1 = clamp(floor(texPos.x + sign(dx)) + 0.5, 0, texW - 1);
    int y1 = clamp(floor(texPos.y + sign(dy)) + 0.5, 0, texH - 1);

    // Cargar texels vecinos
    float4 c00 = tex.Load(int3(x0, y0, 0));
    float4 c10 = tex.Load(int3(x1, y0, 0));
    float4 c01 = tex.Load(int3(x0, y1, 0));
    float4 c11 = tex.Load(int3(x1, y1, 0));

    // Interpolación automática branchless
    float4 top    = lerp(c00, c10, abs(dx));
    float4 bottom = lerp(c01, c11, abs(dx));
    float4 result = lerp(top, bottom, abs(dy));

    return result;
}

// -------------------------------------
// Pixel Shader 1: Escalado entero X e Y (Point sampling)
// -------------------------------------
float4 PS_Point(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    uint texW_u, texH_u;
    tex.GetDimensions(texW_u, texH_u);
    int texW = int(texW_u);
    int texH = int(texH_u);

    int x = clamp(int(uv.x * texW), 0, texW - 1);
    int y = clamp(int(uv.y * texH), 0, texH - 1);

    return tex.Load(int3(x, y, 0));
}

// -------------------------------------
// Pixel Shader 2: Fraccional X, entero Y
// -------------------------------------
float4 PS_FracX(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    uint texW_u, texH_u;
    tex.GetDimensions(texW_u, texH_u);
    int texW = int(texW_u);
    int texH = int(texH_u);

    float2 texPos = uv * float2(texW, texH);

    int x0 = clamp(int(floor(texPos.x)), 0, texW - 1);
    int x1 = min(x0 + 1, texW - 1);
    int y0 = clamp(int(floor(texPos.y)), 0, texH - 1);

    float fx = texPos.x - float(x0);

    float4 c0 = tex.Load(int3(x0, y0, 0));
    float4 c1 = tex.Load(int3(x1, y0, 0));

    return lerp(c0, c1, fx);
}

// -------------------------------------
// Pixel Shader 3: Entero X, fraccional Y
// -------------------------------------
float4 PS_FracY(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    uint texW_u, texH_u;
    tex.GetDimensions(texW_u, texH_u);
    int texW = int(texW_u);
    int texH = int(texH_u);

    float2 texPos = uv * float2(texW, texH);

    int y0 = clamp(int(floor(texPos.y)), 0, texH - 1);
    int y1 = min(y0 + 1, texH - 1);
    int x0 = clamp(int(floor(texPos.x)), 0, texW - 1);

    float fy = texPos.y - float(y0);

    float4 c0 = tex.Load(int3(x0, y0, 0));
    float4 c1 = tex.Load(int3(x0, y1, 0));

    return lerp(c0, c1, fy);
}
