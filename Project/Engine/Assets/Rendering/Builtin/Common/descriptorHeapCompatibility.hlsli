#ifndef NEM_DESCRIPTOR_HEAP_COMPATIBILITY_HLSLI
#define NEM_DESCRIPTOR_HEAP_COMPATIBILITY_HLSLI

#if defined(NEM_DESCRIPTOR_TABLE_COMPAT)

Texture2D<float4> gNEMGlobalTexture2D[] : register(t0, space126);
TextureCube<float4> gNEMGlobalTextureCube[] : register(t0, space127);

#define NEM_TEXTURE2D(index) \
	gNEMGlobalTexture2D[NonUniformResourceIndex(index)]
#define NEM_TEXTURECUBE(index) \
	gNEMGlobalTextureCube[NonUniformResourceIndex(index)]

#else

#define NEM_TEXTURE2D(index) \
	ResourceDescriptorHeap[NonUniformResourceIndex(index)]
#define NEM_TEXTURECUBE(index) \
	ResourceDescriptorHeap[NonUniformResourceIndex(index)]

#endif

#endif
