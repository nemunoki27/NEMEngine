#include "DecalCollector.h"

//============================================================================
//	include
//============================================================================
#include "DecalProjection.h"
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

	// Instanceの指定を優先して標準Materialの値を読む
	template<typename T>
	T Read(const Engine::MaterialParameterSet& defaults, const Engine::MaterialParameterSet& instance,
		const char* name, T fallback) {

		const auto id = Engine::MaterialParameterID::FromName(name);
		const auto* parameter = instance.Find(id);
		if (!parameter) parameter = defaults.Find(id);
		const auto* value = parameter ? std::get_if<T>(&parameter->value) : nullptr;
		if constexpr (std::is_same_v<T, float>) {
			return value && std::isfinite(*value) ? *value : fallback;
		} else if constexpr (std::is_same_v<T, Engine::Color4>) {
			return value && std::isfinite(value->r) && std::isfinite(value->g) &&
				std::isfinite(value->b) && std::isfinite(value->a) ? *value : fallback;
		} else {
			return value ? *value : fallback;
		}
	}

	// 必要なTextureだけを要求して未完了の投影を保留する
	bool ResolveTextures(Engine::GraphicsCore& graphicsCore, const Engine::SceneExecutionContext& context,
		const Engine::MaterialParameterSet& defaults, const Engine::MaterialParameterSet& instance, Engine::DecalDrawItem& item) {

		auto& data = item.gpu;
		const char* names[]{"baseColorTexture", "normalTexture", "metallicRoughnessTexture", "emissiveTexture"};
		uint32_t* indices[]{&data.baseColorTexture, &data.normalTexture, &data.metallicRoughnessTexture, &data.emissiveTexture};
		bool ready = true;
		for (uint32_t index = 0; index < 4; ++index) {
			// 色TextureのAlphaは全チャンネル共通の形状として使う
			if (index > 0 && (data.channels & (1u << index)) == 0) continue;
			const auto id = Read(defaults, instance, names[index], Engine::AssetID{});
			if (!id) continue;
			const auto result = Engine::RuntimeTextureResolver::ResolveBindless(graphicsCore, context.assetDatabase, id,
				index == 0 || index == 3 ? Engine::TextureColorSpace::SRGB : Engine::TextureColorSpace::Linear, index == 1);
			const auto* error = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
			*indices[index] = result.srvIndex;
			item.textures[index] = result.ownership;
			ready &= !result.retry && result.srvIndex != UINT32_MAX && (!error || result.srvIndex != error->srvIndex);
		}
		return ready;
	}
}

bool Engine::DecalCollector::ResolveMaterial(GraphicsCore& graphicsCore, RenderAssetLibrary& assets,
	const SceneExecutionContext& context, const DecalProjectorComponent& component, float opacity, DecalDrawItem& item) {

	static const MaterialAsset fallback = CreateDefaultDecalMaterialAsset("DefaultDecal");
	static std::unordered_set<AssetID> unsupportedMaterials;
	const MaterialAsset* material = component.material ? assets.LoadMaterial(component.material) : &fallback;
	if (!material) return false;
	if (material->domain != MaterialDomain::Decal || material->shaderGraph) {
		if (unsupportedMaterials.insert(component.material).second) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "[Decal] デカール用の標準Materialを設定してください");
		}
		return false;
	}
	unsupportedMaterials.erase(component.material);
	const auto& defaults = material->parameters;
	const auto& instance = component.materialInstance;
	auto& data = item.gpu;
	data.worldToDecal = item.projection.worldToDecal;
	const auto& matrix = item.projection.decalToWorld;
	data.tangentX = Vector4(matrix.m[0][0], matrix.m[0][1], matrix.m[0][2], 0.0f);
	data.tangentY = Vector4(matrix.m[1][0], matrix.m[1][1], matrix.m[1][2], 0.0f);
	data.direction = Vector4(matrix.m[2][0], matrix.m[2][1], matrix.m[2][2], 0.0f);
	data.baseColor = Read(defaults, instance, "color", Color4::White());
	data.baseColor.r *= component.color.r; data.baseColor.g *= component.color.g;
	data.baseColor.b *= component.color.b; data.baseColor.a *= component.color.a;
	data.emissive = Read(defaults, instance, "emissiveColor", Color4::White());
	const float emission = std::max(0.0f, Read(defaults, instance, "emissiveIntensity", 0.0f));
	data.emissive.r *= emission; data.emissive.g *= emission; data.emissive.b *= emission;
	data.surface = Vector4(std::clamp(Read(defaults, instance, "metallic", 0.0f), 0.0f, 1.0f),
		std::clamp(Read(defaults, instance, "roughness", 0.5f), 0.0f, 1.0f),
		std::max(0.0f, Read(defaults, instance, "normalStrength", 1.0f)),
		std::clamp(Read(defaults, instance, "opacity", 1.0f) * opacity, 0.0f, 1.0f));
	data.uvTransform = Vector4(component.uvScale.x, component.uvScale.y, component.uvBias.x, component.uvBias.y);
	const float start = std::clamp(component.startAngleFade, 0.0f, 89.9f);
	const float end = std::clamp(component.endAngleFade, start + 0.01f, 90.0f);
	data.fade = Vector4(std::cos(start * std::numbers::pi_v<float> / 180.0f),
		std::cos(end * std::numbers::pi_v<float> / 180.0f), component.uvRotation * std::numbers::pi_v<float> / 180.0f, 0.0f);
	data.receiverMask = component.receiverRenderingLayerMask & kRenderingLayerMaskBits;
	data.channels = (Read(defaults, instance, "affectColor", true) ? 1u : 0u) |
		(Read(defaults, instance, "affectNormal", false) ? 2u : 0u) |
		(Read(defaults, instance, "affectSurface", false) ? 4u : 0u) |
		(Read(defaults, instance, "affectEmission", false) ? 8u : 0u);
	return data.channels && data.receiverMask && ResolveTextures(graphicsCore, context, defaults, instance, item);
}

void Engine::DecalCollector::Collect(GraphicsCore& graphicsCore, RenderAssetLibrary& assets,
	const SceneExecutionContext& context, std::vector<DecalDrawItem>& output) {

	output.clear();
	if (!context.world || !context.view) return;
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) return;
	const uint32_t cameraMask = context.view->GetCullingMask(RenderCameraDomain::Perspective);
	context.world->ForEach<TransformComponent, DecalProjectorComponent>(
		[&](Entity entity, const TransformComponent& transform, const DecalProjectorComponent& component) {

			if (!component.enabled || !std::isfinite(component.fadeFactor) || component.fadeFactor <= 0.0f) return;
			if (!std::isfinite(component.uvScale.x) || !std::isfinite(component.uvScale.y) ||
				!std::isfinite(component.uvBias.x) || !std::isfinite(component.uvBias.y) || !std::isfinite(component.uvRotation) ||
				!std::isfinite(component.startAngleFade) || !std::isfinite(component.endAngleFade) ||
				!std::isfinite(component.color.r) || !std::isfinite(component.color.g) ||
				!std::isfinite(component.color.b) || !std::isfinite(component.color.a)) return;
			uint32_t mask = component.renderingLayerMask;
			if (const auto* object = context.world->TryGetComponent<SceneObjectComponent>(entity)) {
				if (!object->activeInHierarchy || (context.sceneInstance && object->sceneInstanceID != context.sceneInstance->instanceID)) return;
				mask &= object->visibilityLayerMask;
			}
			if ((mask & cameraMask) == 0) return;
			DecalDrawItem item;
			if (!DecalProjectionUtility::Build(component, transform.worldMatrix, item.projection)) return;
			const float distance = (item.projection.center - camera->cameraPos).Length();
			const float opacity = component.fadeFactor *
				DecalProjectionUtility::DistanceFade(distance, component.drawDistance, component.fadeScale);
			if (opacity <= 0.0f) return;
			item.entity = entity;
			item.sortOrder = component.sortOrder;
			if (ResolveMaterial(graphicsCore, assets, context, component, opacity, item)) output.push_back(std::move(item));
		});
	// 同じ順序の投影もEntityの世代まで比較して安定させる
	std::sort(output.begin(), output.end(), [](const auto& lhs, const auto& rhs) {
		if (lhs.sortOrder != rhs.sortOrder) return lhs.sortOrder < rhs.sortOrder;
		if (lhs.entity.index != rhs.entity.index) return lhs.entity.index < rhs.entity.index;
		return lhs.entity.generation < rhs.entity.generation;
	});
}
