#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Shaders/ShaderCookStorage.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
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

	bool TestMaterialParameters() {

		// 空のアセット参照も保存前の型で復元する
		Engine::MaterialParameterValue emptyTexture{ .value = Engine::AssetID{} };
		Engine::MaterialParameterValue restoredTexture{};
		if (!Engine::ParseMaterialParameterValue(
			Engine::SerializeMaterialParameterValue(emptyTexture), restoredTexture) ||
			!std::holds_alternative<Engine::AssetID>(restoredTexture.value) ||
			std::get<Engine::AssetID>(restoredTexture.value)) {
			return false;
		}

		if (Engine::ResolveMaterialParameterSemantic("occlusionTexture") !=
			Engine::MaterialParameterSemantic::AmbientOcclusionTexture ||
			Engine::ResolveMaterialParameterSemantic("sampleCount") !=
			Engine::MaterialParameterSemantic::None) {
			return false;
		}

		Engine::MaterialParameterSet parameters{};
		Engine::MaterialParameterValue color{};
		color.value = Engine::Color4(0.25f, 0.5f, 0.75f, 1.0f);
		parameters.Set(
			Engine::MaterialParameterIDs::BaseColor,
			Engine::MaterialParameterNames::BaseColor,
			Engine::MaterialParameterSemantic::BaseColor,
			color);

		const Engine::MaterialParameterValue* byID =
			parameters.Find(Engine::MaterialParameterIDs::BaseColor);
		const Engine::MaterialParameterValue* bySemantic =
			parameters.Find(Engine::MaterialParameterSemantic::BaseColor);
		const uint64_t hash = parameters.GetContentHash();
		if (!byID || !bySemantic || byID != bySemantic || hash == 0 ||
			parameters.GetContentHash() != hash) {

			return false;
		}

		const uint64_t readRevision = parameters.GetRevision();
		const Engine::MaterialParameterValue* readColor = parameters.Find(Engine::MaterialParameterIDs::BaseColor);
		parameters.FindByName(Engine::MaterialParameterNames::BaseColor);
		parameters.Find(Engine::MaterialParameterSemantic::BaseColor);
		parameters.begin();
		parameters.find(std::string(Engine::MaterialParameterNames::BaseColor));
		if (!readColor || parameters.GetRevision() != readRevision || parameters.GetContentHash() != hash) {
			return false;
		}
		Engine::MaterialParameterValue editedColor = *readColor;
		editedColor.value = Engine::Color4(1.0f, 0.5f, 0.75f, 1.0f);
		parameters.Set(Engine::MaterialParameterNames::BaseColor, editedColor);
		if (parameters.GetContentHash() == hash || parameters.GetRevision() == readRevision) {
			return false;
		}

		Engine::MaterialParameterValue renamed{};
		renamed.value = Engine::Vector2(2.0f, 4.0f);
		parameters.Set(
			Engine::MaterialParameterIDs::BaseColor,
			"RenamedParameter",
			Engine::MaterialParameterSemantic::None,
			renamed);
		const Engine::MaterialParameterValue* renamedValue =
			parameters.Find(
				Engine::MaterialParameterIDs::BaseColor);
		if (parameters.size() != 1 ||
			parameters.FindByName(
				Engine::MaterialParameterNames::BaseColor) != nullptr ||
			parameters.FindByName("RenamedParameter") == nullptr ||
			!renamedValue ||
			!std::holds_alternative<Engine::Vector2>(
				renamedValue->value)) {

			return false;
		}

		// Shader GraphのUUID由来IDへ名前指定の実行時値を重ねられることを確認する
		const Engine::MaterialParameterID graphParameterID{
			0x94d20ddddf0f9c93ull };
		// コピー用のIDが小文字16桁で先頭の0を保持することを確認する
		if (Engine::ToString(Engine::UUID{ graphParameterID.value }) != "94d20ddddf0f9c93" ||
			Engine::ToString(Engine::UUID{ 1 }) != "0000000000000001" ||
			Engine::ToString(Engine::UUID{ 0xffffffffffffffffull }) != "ffffffffffffffff") {

			return false;
		}
		Engine::MaterialParameterSet graphDefaults{};
		Engine::MaterialParameterValue defaultThreshold{};
		defaultThreshold.value = 0.5f;
		graphDefaults.Set(graphParameterID, "Threshold",
			Engine::MaterialParameterSemantic::None, defaultThreshold);
		Engine::MaterialParameterSet scriptOverrides{};
		Engine::MaterialParameterValue scriptThreshold{};
		scriptThreshold.value = 0.75f;
		scriptOverrides.Set(
			Engine::MaterialParameterID::FromName("Threshold"),
			"Threshold", Engine::MaterialParameterSemantic::None,
			scriptThreshold);

		Engine::MaterialParameterSet merged = graphDefaults;
		merged.MergeFrom(scriptOverrides);
		const Engine::MaterialParameterValue* mergedThreshold =
			merged.Find(graphParameterID);
		if (merged.size() != 1 || !mergedThreshold ||
			!std::holds_alternative<float>(mergedThreshold->value) ||
			std::get<float>(mergedThreshold->value) != 0.75f) {

			return false;
		}

		Engine::ShaderConstantBufferVariable thresholdVariable{};
		thresholdVariable.name = "p_Threshold_df0f9c93";
		thresholdVariable.parameterID = graphParameterID;
		thresholdVariable.size = sizeof(float);
		thresholdVariable.valueClass = D3D_SVC_SCALAR;
		thresholdVariable.valueType = D3D_SVT_FLOAT;
		Engine::ShaderConstantBufferInfo parameterBuffer{};
		parameterBuffer.name = Engine::MaterialParameterCBuffer::kSurface;
		parameterBuffer.size = 16;
		parameterBuffer.variables.emplace_back(thresholdVariable);
		Engine::ShaderReflectionInfo parameterReflection{};
		parameterReflection.constantBuffers.emplace_back(parameterBuffer);
		Engine::MaterialParameterLayout parameterLayout{};
		parameterLayout.Build(parameterReflection);
		const std::vector<uint8_t> packed =
			Engine::MaterialParameterBufferBuilder::BuildElement(
				graphDefaults, scriptOverrides, parameterLayout, {});
		float packedThreshold = 0.0f;
		if (packed.size() < sizeof(packedThreshold)) {
			return false;
		}
		std::memcpy(&packedThreshold, packed.data(), sizeof(packedThreshold));
		if (packedThreshold != 0.75f) {
			return false;
		}

		// Cook後も公開ParameterのIDとGPU配置を維持する
		Engine::ShaderReflectionInfo cookedReflection{};
		const nlohmann::json cookedData = Engine::ShaderCookStorage::WriteReflection(parameterReflection);
		if (!Engine::ShaderCookStorage::ReadReflection(cookedData, cookedReflection) ||
			cookedReflection.constantBuffers.size() != 1 ||
			cookedReflection.constantBuffers.front().variables.size() != 1 ||
			cookedReflection.constantBuffers.front().variables.front().parameterID != graphParameterID) {
			return false;
		}
		Engine::MaterialParameterLayout cookedLayout{};
		cookedLayout.Build(cookedReflection);
		if (Engine::MaterialParameterBufferBuilder::BuildElement(graphDefaults, scriptOverrides, cookedLayout, {}) != packed ||
			Engine::ShaderCookStorage::ReadReflection(nlohmann::json::array(), cookedReflection)) {
			return false;
		}

		// Materialとサブメッシュの表面方式がJSON往復後も維持されることを確認する
		Engine::MaterialAsset material{};
		material.renderState.overridesRenderer = true;
		material.renderState.surfaceMode =
			Engine::MaterialSurfaceMode::Masked;
		Engine::MaterialAsset restoredMaterial{};
		if (!Engine::FromJson(
			Engine::ToJson(material), restoredMaterial) ||
			restoredMaterial.renderState.surfaceMode !=
				Engine::MaterialSurfaceMode::Masked) {

			return false;
		}

		Engine::SubMeshMaterial subMesh{};
		subMesh.surfaceMode = Engine::MaterialSurfaceMode::Auto;
		subMesh.sourceSurfaceMode =
			Engine::MaterialSurfaceMode::Transparent;
		subMesh.alphaCutoff = 0.37f;
		const Engine::SubMeshMaterial restoredSubMesh =
			nlohmann::json(subMesh).get<Engine::SubMeshMaterial>();
		return restoredSubMesh.surfaceMode ==
				Engine::MaterialSurfaceMode::Auto &&
			restoredSubMesh.sourceSurfaceMode ==
				Engine::MaterialSurfaceMode::Transparent &&
			std::abs(restoredSubMesh.alphaCutoff - 0.37f) < 1e-6f &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Masked) ==
				Engine::RenderPhase::Opaque &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Transparent,
				Engine::RenderPhase::Opaque) ==
				Engine::RenderPhase::Transparent &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Transparent,
				Engine::RenderPhase::ScreenUI) ==
				Engine::RenderPhase::ScreenUI &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Masked,
				Engine::RenderPhase::PostProcessUI) ==
				Engine::RenderPhase::PostProcessUI;
	}

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
		constexpr int32_t colorProperty = 0;
		constexpr int32_t enabledProperty = 1;
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
