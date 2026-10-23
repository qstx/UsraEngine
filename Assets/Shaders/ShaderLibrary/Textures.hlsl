#ifndef URSA_SHADER_LIBRARY_TEXTURES_INCLUDED
#define URSA_SHADER_LIBRARY_TEXTURES_INCLUDED

#ifndef TEXTURE2D
#define TEXTURE2D(name) Texture2D<float4> name
#endif
#ifndef SAMPLER
#define SAMPLER(name) SamplerState name
#endif
#ifndef SAMPLE_TEXTURE2D
#define SAMPLE_TEXTURE2D(textureName, samplerName, uv) textureName.Sample(samplerName, uv)
#endif
#ifndef SAMPLE_TEXTURE2D_LOD
#define SAMPLE_TEXTURE2D_LOD(textureName, samplerName, uv, lod) textureName.SampleLevel(samplerName, uv, lod)
#endif

TEXTURE2D(_CameraColorTexture);
SAMPLER(sampler_CameraColorTexture);
TEXTURE2D(_CameraDepthTexture);
SAMPLER(sampler_CameraDepthTexture);
TEXTURE2D(_CameraNormalsTexture);
SAMPLER(sampler_CameraNormalsTexture);

#endif
