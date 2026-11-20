#include "DecalCollector.h"

//============================================================================
//	include
//============================================================================
#include "DecalProjection.h"
#include "DecalGraphProgram.h"
#include <bit>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/World/Components/Rendering/HexTilingComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
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

	bool ResolveGraph(Engine::GraphicsCore& graphicsCore, const Engine::SceneExecutionContext& context,
		const Engine::MaterialAsset& material, const Engine::MaterialParameterSet& instance, Engine::DecalDrawItem& item) {

		using namespace Engine;
		// 入力レジスタに使わない上位ビットで法線の回転補正を指定する
		constexpr uint32_t kNormalSample = 0x80u;
		const auto& program = *material.decalGraph;
		item.gpu.channels = program.channels;
		item.gpu.graphCount = static_cast<uint32_t>(program.instructions.size());
		item.graphWords.reserve(program.instructions.size() * 6);
		for (const auto& command : program.instructions) {
			const auto* parameter = instance.Find(command.parameter);
			if (!parameter) parameter = material.parameters.Find(command.parameter);
			Vector4 value = parameter ? DecalGraphValue(*parameter) : command.value;
			item.graphWords.push_back(static_cast<uint32_t>(command.op));
			uint32_t inputs = command.inputs;
			if (command.op == DecalGraphOp::NormalUnpack) {
				const uint32_t input = inputs & 63u;
				if (input >= 4u && input - 4u < item.graphWords.size() / 6u) {
					const size_t source = (input - 4u) * 6u;
					if (item.graphWords[source] == static_cast<uint32_t>(DecalGraphOp::TextureSample)) {
						item.graphWords[source + 1] |= kNormalSample;
					}
				}
			}
			if (command.op == DecalGraphOp::TextureSample) {
				const uint32_t input = inputs & 63u;
				if (input >= 4u && input - 4u < program.instructions.size() &&
					program.instructions[input - 4u].semantic == MaterialParameterSemantic::NormalTexture) inputs |= kNormalSample;
			}
			item.graphWords.push_back(inputs);
			uint32_t x = std::bit_cast<uint32_t>(value.x);
			if (command.op == DecalGraphOp::Texture) {
				const auto* overrideID = parameter ? std::get_if<AssetID>(&parameter->value) : nullptr;
				const auto texture = overrideID ? *overrideID : command.texture;
				x = UINT32_MAX;
				if (texture) {
					const auto semantic = command.semantic;
					const bool color = semantic == MaterialParameterSemantic::BaseColorTexture || semantic == MaterialParameterSemantic::EmissiveTexture;
					const auto resolved = RuntimeTextureResolver::ResolveBindless(graphicsCore, context.assetDatabase, texture,
						color ? TextureColorSpace::SRGB : TextureColorSpace::Linear, semantic == MaterialParameterSemantic::NormalTexture);
					if (resolved.retry || resolved.srvIndex == UINT32_MAX) return false;
					x = resolved.srvIndex;
					item.graphTextures.push_back(resolved.ownership);
				}
			}
			item.graphWords.insert(item.graphWords.end(), {x, std::bit_cast<uint32_t>(value.y),
				std::bit_cast<uint32_t>(value.z), std::bit_cast<uint32_t>(value.w)});
		}
		return true;
	}
}

bool Engine::DecalCollector::ResolveMaterial(GraphicsCore& graphicsCore, RenderAssetLibrary& assets,
	const SceneExecutionContext& context, const DecalProjectorComponent& component, float opacity, DecalDrawItem& item) {

	static std::unordered_set<AssetID> unsupportedMaterials;
	// 個別指定がなければプロジェクトの既定を使う
	const AssetID materialID = context.assetDatabase ?
		MaterialResolver{}.ResolveORDefault(*context.assetDatabase, component.material, DefaultMaterialSlot::Decal) : component.material;
	const MaterialAsset* material = assets.LoadMaterial(materialID);
	if (!material) return false;
	if (material->domain != MaterialDomain::Decal || (material->shaderGraph && !material->decalGraph)) {
		if (unsupportedMaterials.insert(materialID).second) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "[Decal] 有効なデカール用Materialを設定してください");
		}
		return false;
	}
	unsupportedMaterials.erase(materialID);
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
	const float start = std::clamp(component.startAngleFade, 0.0f, 89.9f);
	const float end = std::clamp(component.endAngleFade, start + 0.01f, 90.0f);
	data.fade = Vector4(std::cos(start * std::numbers::pi_v<float> / 180.0f),
		std::cos(end * std::numbers::pi_v<float> / 180.0f), 0.0f, 0.0f);
	data.receiverMask = component.receiverRenderingLayerMask & kRenderingLayerMaskBits;
	data.channels = (Read(defaults, instance, "affectColor", true) ? 1u : 0u) |
		(Read(defaults, instance, "affectNormal", false) ? 2u : 0u) |
		(Read(defaults, instance, "affectSurface", false) ? 4u : 0u) |
		(Read(defaults, instance, "affectEmission", false) ? 8u : 0u);
	if (material->decalGraph) return data.receiverMask && ResolveGraph(graphicsCore, context, *material, instance, item);
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
			if (!ResolveMaterial(graphicsCore, assets, context, component, opacity, item)) return;
			const auto* transformUV = context.world->TryGetComponent<UVTransformComponent>(entity);
			const auto matrix = DecalProjectionUtility::ResolveUVMatrix(component, transformUV);
			item.gpu.hexTiling = ResolveHexTiling(*context.world, entity);
			item.gpu.uvTransform = {matrix.m[0][0], matrix.m[0][1], matrix.m[1][0], matrix.m[1][1]};
			item.gpu.fade.z = matrix.m[3][0];
			item.gpu.fade.w = matrix.m[3][1];
			output.push_back(std::move(item));
		});
	// 同じ順序の投影もEntityの世代まで比較して安定させる
	std::sort(output.begin(), output.end(), [](const auto& lhs, const auto& rhs) {
		if (lhs.sortOrder != rhs.sortOrder) return lhs.sortOrder < rhs.sortOrder;
		if (lhs.entity.index != rhs.entity.index) return lhs.entity.index < rhs.entity.index;
		return lhs.entity.generation < rhs.entity.generation;
	});
}
