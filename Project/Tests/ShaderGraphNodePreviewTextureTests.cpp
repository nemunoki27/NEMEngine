#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"

//============================================================================
//	include
//============================================================================

// c++
#include <array>

using namespace Engine;

bool NEMTests::TestShaderGraphNodePreviewTextures(ShaderGraphNodePreviewGPUFixture& fixture) {

	const auto point = CreateShaderGraphNodePreviewTextureCase({.25f, .25f}, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_FILTER_MIN_MAG_MIP_POINT);
	const std::array expected{
		Vector4(1, 0, 0, .25f), Vector4(1, 0, 0, 1), Vector4(1, 1, 1, 1),
		Vector4(0, 0, 0, 1), Vector4(0, 0, 0, 1), Vector4(.25f, .25f, .25f, 1),
	};
	for (uint32_t slot = 0; slot < expected.size(); ++slot) {
		if (!ExpectShaderGraphPreview(fixture, point, expected[slot], slot)) return false;
	}
	const auto clamp = CreateShaderGraphNodePreviewTextureCase({1.25f, .25f}, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_FILTER_MIN_MAG_MIP_POINT);
	const auto wrap = CreateShaderGraphNodePreviewTextureCase({1.25f, .25f}, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_FILTER_MIN_MAG_MIP_POINT);
	const auto mirror = CreateShaderGraphNodePreviewTextureCase({1.25f, .25f}, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_FILTER_MIN_MAG_MIP_POINT);
	const auto linear = CreateShaderGraphNodePreviewTextureCase({.5f, .5f}, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
	if (!ExpectShaderGraphPreview(fixture, clamp, {0, 1, 0, .5f}) || !ExpectShaderGraphPreview(fixture, wrap, {1, 0, 0, .25f}) ||
		!ExpectShaderGraphPreview(fixture, mirror, {0, 1, 0, .5f}) || !ExpectShaderGraphPreview(fixture, linear, {.5f, .5f, .5f, .625f})) return false;
	return fixture.CheckMessages();
}
