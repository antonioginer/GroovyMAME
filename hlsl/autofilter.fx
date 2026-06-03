// license:BSD-3-Clause
// copyright-holders:Antonio Giner
//============================================================
//
// Autofilter.fx
//
//============================================================

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
	float2 positions[3] =
	{
		float2(-1.0, -1.0),
		float2(-1.0,  3.0),
		float2( 3.0, -1.0)
	};

	float2 uvs[3] =
	{
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

cbuffer factors : register(b0)
{
	float x_factor;
	float y_factor;
};

// ---------------------------------------------------------------
// Pixel shader for automatic interpolation based on scale factors
// ---------------------------------------------------------------

float4 PS_Main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
	// Uncomment for raw sampling
	// return tex.Sample(samp, uv);

	// Get texture dimensions
	uint texW_u, texH_u;
	tex.GetDimensions(texW_u, texH_u);
	int texW = int(texW_u);
	int texH = int(texH_u);

	// Position in texel space
	float2 texPos = uv * float2(texW, texH);

	// Distance to closest texel
	float dx = frac(texPos.x) - 0.5;
	float dy = frac(texPos.y) - 0.5;

	// Position of neighbour texels
	int x0 = int(texPos.x);
	int y0 = int(texPos.y);
	int x1 = clamp(floor(texPos.x + x_factor * float(sign(dx))), 0, texW - 1);
	int y1 = clamp(floor(texPos.y + y_factor * float(sign(dy))), 0, texH - 1);

	// Load neighbour texels
	float4 c00 = tex.Load(int3(x0, y0, 0));
	float4 c10 = tex.Load(int3(x1, y0, 0));
	float4 c01 = tex.Load(int3(x0, y1, 0));
	float4 c11 = tex.Load(int3(x1, y1, 0));

	// Automatic branchless interpolation
	float4 top    = lerp(c00, c10, abs(dx));
	float4 bottom = lerp(c01, c11, abs(dx));
	float4 result = lerp(top, bottom, abs(dy));

	return result;
}
