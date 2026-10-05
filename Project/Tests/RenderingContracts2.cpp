#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingMaterialResolver.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Shaders/ShaderCookStorage.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestScreenSpaceOutlineBinding() {

		using namespace Engine;
		using namespace Engine::GeneratedComponentBindings;
		ECSWorld world(ECSWorldKind::Runtime);
		auto& registry = ManagedWorldRegistry::GetInstance();
		const ManagedWorldHandle handle = registry.Register(world);
		const Entity entity = SceneAuthoring::CreateGameObject(world, "OutlineBinding");
		world.AddComponent<ScreenSpaceOutlineComponent>(entity);
		const ManagedNativeEntity native = MakeNativeEntity(world, entity);
		constexpr int32_t typeID = 24;
		constexpr int32_t enabledProperty = 0;
		constexpr int32_t colorProperty = 1;
		bool passed = true;

		for (float alpha : { 1.0f, 0.5f, 0.0f, 1.0f }) {
			const Color4 color(0.25f, 0.5f, 0.75f, alpha);
			Color4 restored{};
			const uint64_t revision = world.GetRenderDataRevision();
			passed &= SetComponentProperty(native, typeID, colorProperty, &color, sizeof(color)) == ManagedStatus::Ok;
			passed &= world.GetRenderDataRevision() > revision;
			passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) == ManagedStatus::Ok;
			passed &= std::memcmp(&color, &restored, sizeof(color)) == 0;
		}

		for (int32_t enabled : { 0, 1 }) {
			int32_t restored = -1;
			passed &= SetComponentProperty(native, typeID, enabledProperty, &enabled, sizeof(enabled)) == ManagedStatus::Ok;
			passed &= GetComponentProperty(native, typeID, enabledProperty, &restored, sizeof(restored)) == ManagedStatus::Ok;
			passed &= restored == enabled;
		}

		// 不正な書き込みで既存の色を壊さない
		const Color4 color = world.GetComponent<ScreenSpaceOutlineComponent>(entity).color;
		Color4 restored{};
		passed &= SetComponentProperty(native, typeID, colorProperty, &restored, 4) == ManagedStatus::InvalidArgument;
		passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) == ManagedStatus::Ok;
		passed &= std::memcmp(&color, &restored, sizeof(color)) == 0;
		passed &= GetComponentProperty(native, typeID, 99, &restored, sizeof(restored)) == ManagedStatus::InvalidArgument;

		// コンポーネントの再追加後もハンドルから引き直す
		world.RemoveComponent<ScreenSpaceOutlineComponent>(entity);
		passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) == ManagedStatus::InvalidArgument;
		world.AddComponent<ScreenSpaceOutlineComponent>(entity);
		passed &= SetComponentProperty(native, typeID, colorProperty, &color, sizeof(color)) == ManagedStatus::Ok;
		registry.Unregister(handle);
		passed &= GetComponentProperty(native, typeID, colorProperty, &restored, sizeof(restored)) != ManagedStatus::Ok;
		return passed;
	}

	bool TestScreenSpaceOutlineSerialization() {

		Engine::ScreenSpaceOutlineComponent source{};
		source.alphaSource =
			Engine::ScreenSpaceOutlineAlphaSource::TextureColor;
		source.uiOcclusionMode =
			Engine::ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible;

		nlohmann::json serialized{};
		Engine::to_json(serialized, source);
		Engine::ScreenSpaceOutlineComponent restored{};
		Engine::from_json(serialized, restored);

		return serialized.value("alphaSource", std::string{}) ==
			"TextureColor" &&
			serialized.value("uiOcclusionMode", std::string{}) ==
				"AlwaysVisible" &&
			restored.alphaSource ==
				Engine::ScreenSpaceOutlineAlphaSource::TextureColor &&
			restored.uiOcclusionMode ==
				Engine::ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible;
	}

	bool TestTextureImportSettings() {

		const Engine::TextureImportSettings color =
			Engine::MakeTextureImportSettings(Engine::TextureImportPreset::Color);
		if (color.colorSpace != Engine::TextureColorSpace::SRGB ||
			color.filter != Engine::TextureFilterMode::Anisotropic ||
			!color.generateMipmaps || !color.alphaColorBleed) {
			return false;
		}

		const Engine::TextureImportSettings normal =
			Engine::MakeTextureImportSettings(Engine::TextureImportPreset::NormalMap);
		if (normal.colorSpace != Engine::TextureColorSpace::Linear ||
			normal.alphaColorBleed ||
			Engine::ToD3D12Filter(normal) != D3D12_FILTER_ANISOTROPIC) {
			return false;
		}

		const nlohmann::json data = {
			{ "preset", "Data" },
			{ "filter", "Point" },
			{ "addressU", "Mirror" },
			{ "maxAnisotropy", 99 },
		};
		const Engine::TextureImportSettings parsed =
			Engine::ParseTextureImportSettings(data);
		if (parsed.preset != Engine::TextureImportPreset::Data ||
			parsed.colorSpace != Engine::TextureColorSpace::Linear ||
			parsed.filter != Engine::TextureFilterMode::Point ||
			parsed.addressU != Engine::TextureAddressMode::Mirror ||
			parsed.maxAnisotropy != 16 ||
			Engine::ToD3D12AddressMode(parsed.addressU) !=
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR) {
			return false;
		}

		if (Engine::ParseTextureImportSettings(Engine::ToJson(parsed)) != parsed) {
			return false;
		}

		const Engine::TextureImportSettings automatic{};
		return Engine::ResolveTextureColorSpace(
			automatic, Engine::TextureColorSpace::SRGB) ==
				Engine::TextureColorSpace::SRGB &&
			Engine::ResolveTextureColorSpace(
				normal, Engine::TextureColorSpace::SRGB) ==
					Engine::TextureColorSpace::Linear &&
			Engine::HashTextureImportSettings(
				automatic, Engine::TextureColorSpace::SRGB) !=
				Engine::HashTextureImportSettings(
					automatic, Engine::TextureColorSpace::Linear);
	}

	bool TestShaderReflectionMerge() {

		Engine::ShaderConstantBufferVariable vertexVariable{};
		vertexVariable.name = "metallic";
		vertexVariable.parameterID =
			Engine::MaterialParameterID::FromName(
				vertexVariable.name);
		vertexVariable.semantic =
			Engine::MaterialParameterSemantic::Metallic;
		vertexVariable.used = false;

		Engine::ShaderConstantBufferInfo vertexBuffer{};
		vertexBuffer.name = "MaterialParameters";
		vertexBuffer.bindPoint = 3;
		vertexBuffer.size = 16;
		vertexBuffer.variables.emplace_back(vertexVariable);
		Engine::ShaderReflectionInfo reflection{};
		reflection.constantBuffers.emplace_back(vertexBuffer);

		Engine::ShaderConstantBufferVariable pixelVariable =
			vertexVariable;
		pixelVariable.used = true;
		Engine::ShaderConstantBufferInfo pixelBuffer =
			vertexBuffer;
		pixelBuffer.variables.clear();
		pixelBuffer.variables.emplace_back(pixelVariable);
		Engine::ShaderReflectionInfo pixelReflection{};
		pixelReflection.constantBuffers.emplace_back(pixelBuffer);

		Engine::MergeShaderReflection(
			reflection, pixelReflection);
		const Engine::ShaderConstantBufferInfo* merged =
			Engine::FindConstantBuffer(
				reflection, "MaterialParameters");
		return merged && merged->variables.size() == 1 &&
			merged->variables.front().used;
	}
}
