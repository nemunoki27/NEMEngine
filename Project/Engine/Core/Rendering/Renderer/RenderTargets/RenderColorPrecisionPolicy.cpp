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
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticlePaintReceiverComponent.h>
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

	bool IsBoundedColor(const Engine::Color4& color, float limit, bool unitAlpha) {

		for (const float channel : {color.r, color.g, color.b, color.a}) {
			if (!std::isfinite(channel) || std::abs(channel) > limit) return false;
		}
		return !unitAlpha || (color.a >= 0.0f && color.a <= 1.0f);
	}

	bool IsBounded(Engine::GraphicsCore& graphics, const Engine::AssetDatabase* database,
		const Engine::MaterialParameterSet& values, bool unitAlpha = false) {

		using namespace Engine;
		if (const auto* value = values.Find(MaterialParameterIDs::BaseColor)) {
			const auto* color = std::get_if<Color4>(&value->value);
			if (!color || !IsBoundedColor(*color, 16.0f, unitAlpha)) return false;
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

bool Engine::RenderColorPrecisionPolicy::HasBoundedPrimitiveColors(const PrimitiveRendererComponent& renderer) {

	if (IsPrimitiveScreen2D(renderer) || renderer.blendMode != BlendMode::Normal) return false;
	if (const auto* value = renderer.materialInstance.Find(MaterialParameterIDs::BaseColor)) {
		const auto* color = std::get_if<Color4>(&value->value);
		if (!color || !IsBoundedColor(*color, 16.0f, true)) return false;
	}
	if (renderer.type != PrimitiveType::Cylinder) return true;
	// 補間される頂点色を単位範囲に制限してMaterial色の上限を保つ
	return IsBoundedColor(renderer.cylinder.topColor, 1.0f, true) &&
		IsBoundedColor(renderer.cylinder.centerColor, 1.0f, true) &&
		IsBoundedColor(renderer.cylinder.bottomColor, 1.0f, true);
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
	// デカールと描き込みは任意の色を書けるため広い範囲を維持する
	bool hasUnboundedColor = false;
	if (world) {
		world->ForEach<DecalProjectorComponent>([&](const Entity&, const auto&) { hasUnboundedColor = true; });
		world->ForEach<ParticlePaintReceiverComponent>([&](const Entity&, const auto&) { hasUnboundedColor = true; });
	}
	if (hasUnboundedColor) return format_;
	if (!database) return format_;
	MaterialResolver resolver;
	// RTとTextureとして使えるDeviceだけ省メモリ形式を選ぶ
	D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_R16G16B16A16_FLOAT};
	const auto required = D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_BLENDABLE;
	if (FAILED(graphics.GetDXObject().GetDevice()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) ||
		(support.Support1 & required) != required) return format_;
	for (const auto& item : batch.GetItems()) {
		if (item.renderPhase != RenderPhase::Opaque) continue;
		const bool primitive = item.backendID == RenderBackendID::Primitive;
		if (item.backendID != RenderBackendID::Mesh && !primitive) return format_;
		const auto slot = primitive ? DefaultMaterialSlot::Primitive : DefaultMaterialSlot::Mesh;
		const auto* material = assets.LoadMaterial(resolver.ResolveORDefault(*database, item.material, slot));
		if (!material || !MaterialRenderPolicy::IsStandardSurface(*material) ||
			!IsBounded(graphics, database, material->parameters, primitive)) return format_;
		if (primitive) {
			const auto* payload = batch.GetPayload<PrimitiveRenderPayload>(item);
			if (item.blendMode != BlendMode::Normal || !payload || !payload->renderer || !HasBoundedPrimitiveColors(*payload->renderer) ||
				!IsBounded(graphics, database, payload->renderer->materialInstance, true)) return format_;
		}
		if (item.backendID == RenderBackendID::Mesh && item.world) {
			for (const auto& subMesh : GetMeshSubMeshes(*item.world, item.entity)) {
				if (!IsBounded(graphics, database, subMesh.materialInstance)) return format_;
			}
		}
	}
	format_ = DXGI_FORMAT_R16G16B16A16_FLOAT;
	return format_;
}
