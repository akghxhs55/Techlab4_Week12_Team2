struct VSOutput
{
	float4 Position : SV_POSITION;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
	VSOutput Output;
	float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
	Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
	return Output;
}

float4 mainPS(VSOutput Input) : SV_Target
{
	int2 Cell = int2(Input.Position.xy) / 16;
	bool bEven = ((Cell.x + Cell.y) & 1) == 0;
	float Shade = bEven ? 0.055f : 0.098f;
	return float4(Shade, Shade, Shade, 1.0f);
}
