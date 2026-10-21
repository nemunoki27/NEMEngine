#include "RenderColorPrecisionPolicy.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <cmath>

namespace {

	bool IsBoundedFormat(DXGI_FORMAT format) {

		// 浮動小数点Textureと任意のRenderTextureはFP32に残す
		switch (format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: return true;
		default: return false;
		}
	}

	bool IsBounded(Engine::GraphicsCore& graphics, const Engine::AssetDatabase* database,
		const Engine::MaterialParameterSet& values) {

		using namespace Engine;
		if (const auto* value = values.Find(MaterialParameterIDs::BaseColor)) {
			const auto* color = std::get_if<Color4>(&value->value);
			if (!color) return false;
			for (const float channel : {color->r, color->g, color->b, color->a}) {
				if (!std::isfinite(channel) || std::abs(channel) > 16.0f) return false;
			}
		}
		if (const auto* value = values.Find(MaterialParameterIDs::BaseColorTexture)) {
			const auto* id = std::get_if<AssetID>(&value->value);
			if (!id) return false;
			if (*id) {
				const auto* meta = database ? database->Find(*id) : nullptr;
				if (!meta || meta->type != AssetType::Texture) return false;
				const auto* texture = RuntimeTextureResolver::Resolve(graphics, database, *id, TextureColorSpace::SRGB);
				if (!texture || !texture->valid || !texture->resource || !IsBoundedFormat(texture->resource->GetDesc().Format)) return false;
			}
		}
		return true;
	}
}

DXGI_FORMAT Engine::RenderColorPrecisionPolicy::Resolve(GraphicsCore& graphics, RenderAssetLibrary& assets,
	const RenderSceneBatch& batch, const AssetDatabase* database, ECSWorld* world, bool allowCompact) {

	if (!allowCompact) return DXGI_FORMAT_R32G32B32A32_FLOAT;
	InputKey key;
	key.renderRevision = batch.GetSourceRevision();
	key.materialRevision = *assets.GetMaterialRevision();
	Algorithm::HashCombine(key.materialRevision, MaterialRenderPolicy::GetDefaultMaterialHash());
	key.textureRevision = graphics.GetTextureUploadService().GetContentRevision() + RuntimeTextureResolver::GetBindingRevision();
	const auto lifetime = world ? world->GetLifetime() : nullptr;
	if (batch_ == &batch && lifetime_ == lifetime && key_ == key) return format_;
	batch_ = &batch;
	lifetime_ = lifetime;
	key_ = key;
	format_ = DXGI_FORMAT_R32G32B32A32_FLOAT;
	// デカールは任意の色を書けるため広い範囲を維持する
	bool hasDecals = false;
	if (world) world->ForEach<DecalProjectorComponent>([&](const Entity&, const auto&) { hasDecals = true; });
	if (hasDecals) return format_;
	if (!database) return format_;
	MaterialResolver resolver;
	// RTとTextureとして使えるDeviceだけ省メモリ形式を選ぶ
	D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_R16G16B16A16_FLOAT};
	const auto required = D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_BLENDABLE;
	if (FAILED(graphics.GetDXObject().GetDevice()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
		(support.Support1 & required) != required) return format_;
	for (const auto& item : batch.GetItems()) {
		if (item.renderPhase != RenderPhase::Opaque) continue;
		// 形状固有の頂点色や独自BackendはFP32へ戻す
		if (item.backendID != RenderBackendID::Mesh) return format_;
		const auto* material = assets.LoadMaterial(resolver.ResolveORDefault(*database, item.material, DefaultMaterialSlot::Mesh));
		if (!material || !MaterialRenderPolicy::IsStandardSurface(*material) || !IsBounded(graphics, database, material->parameters)) return format_;
		if (item.backendID == RenderBackendID::Mesh && item.world) {
			for (const auto& subMesh : GetMeshSubMeshes(*item.world, item.entity)) {
				if (!IsBounded(graphics, database, subMesh.materialInstance)) return format_;
			}
		}
	}
	format_ = DXGI_FORMAT_R16G16B16A16_FLOAT;
	return format_;
}
