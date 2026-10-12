#ifndef URSA_CORE_INCLUDED
#define URSA_CORE_INCLUDED

#define TEXTURE2D(name) Texture2D<float4> name
#define SAMPLER(name) SamplerState name
#define SAMPLE_TEXTURE2D(texture, samplerName, uv) texture.Sample(samplerName, uv)
#define SAMPLE_TEXTURE2D_LOD(texture, samplerName, uv, lod) texture.SampleLevel(samplerName, uv, lod)
#define CBUFFER_START(name) cbuffer name {
#define CBUFFER_END };

#endif
