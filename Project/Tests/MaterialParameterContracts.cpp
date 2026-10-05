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

	bool TestMaterialParameters() {

		// JSON文字列の往復でも整数の型と全値域を維持する
		for (uint32_t number : { 0u, 1u, 2147483647u, 2147483648u, UINT32_MAX }) {
			Engine::MaterialParameterValue original{ .value = number }, restored;
			const auto encoded = Engine::SerializeMaterialParameterValue(original);
			if (!Engine::ParseMaterialParameterValue(nlohmann::json::parse(encoded.dump()), restored) ||
				!std::holds_alternative<uint32_t>(restored.value) || std::get<uint32_t>(restored.value) != number) return false;
			Engine::MaterialInstanceParameters instance;
			instance.Set("sampleCount", original);
			Engine::MaterialInstanceParameters loaded;
			Engine::ReadMaterialInstance(nlohmann::json::parse(Engine::WriteMaterialInstance(instance).dump()), loaded);
			const auto* value = loaded.FindByName("sampleCount");
			if (!value || !std::holds_alternative<uint32_t>(value->value) || std::get<uint32_t>(value->value) != number) return false;
		}
		for (int32_t number : { INT32_MIN, -1, 0, 1, INT32_MAX }) {
			Engine::MaterialParameterValue original{ .value = number }, restored;
			if (!Engine::ParseMaterialParameterValue(nlohmann::json::parse(Engine::SerializeMaterialParameterValue(original).dump()), restored) ||
				!std::holds_alternative<int32_t>(restored.value) || std::get<int32_t>(restored.value) != number) return false;
		}
		for (int64_t number : { 2147483648LL, 4294967295LL }) {
			Engine::MaterialParameterValue direct, parsed;
			const nlohmann::json value = number;
			if (!Engine::ParseMaterialParameterValue(value, direct) ||
				!Engine::ParseMaterialParameterValue(nlohmann::json::parse(value.dump()), parsed) ||
				std::get<uint32_t>(direct.value) != number || std::get<uint32_t>(parsed.value) != number) return false;
		}
		for (const auto& invalid : { nlohmann::json(4294967296ULL), nlohmann::json(-2147483649LL),
			nlohmann::json({ { "valueType", "uint" }, { "value", -1 } }),
			nlohmann::json({ { "valueType", "uint" }, { "value", 4294967296ULL } }),
			nlohmann::json({ { "valueType", "uint" }, { "value", 1.5 } }) }) {
			Engine::MaterialParameterValue retained{ .value = 42u };
			if (Engine::ParseMaterialParameterValue(invalid, retained) || std::get<uint32_t>(retained.value) != 42u) return false;
		}

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
			Engine::ResolveMaterialParameterSemantic("opacityTexture") !=
			Engine::MaterialParameterSemantic::OpacityTexture ||
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
		// 配置が同じでも参照IDが変われば転送結果と識別値を更新する
		auto redirectedReflection = parameterReflection;
		const Engine::MaterialParameterID redirectedID{ graphParameterID.value + 1 };
		redirectedReflection.constantBuffers[0].variables[0].parameterID = redirectedID;
		graphDefaults.Set(redirectedID, "Replacement", Engine::MaterialParameterSemantic::None,
			Engine::MaterialParameterValue{ .value = 0.25f });
		Engine::MaterialParameterLayout redirectedLayout;
		redirectedLayout.Build(redirectedReflection);
		const auto redirected = Engine::MaterialParameterBufferBuilder::BuildElement(
			graphDefaults, scriptOverrides, redirectedLayout, {});
		float redirectedThreshold = 0.0f;
		if (redirected.size() < sizeof(redirectedThreshold)) return false;
		std::memcpy(&redirectedThreshold, redirected.data(), sizeof(redirectedThreshold));
		if (redirectedThreshold != 0.25f || redirectedLayout.GetSizeInBytes() != parameterLayout.GetSizeInBytes() ||
			redirectedLayout.GetContentHash() == parameterLayout.GetContentHash()) return false;

		// 未指定のuintとTextureだけに使う無効番号を分ける
		auto unsignedReflection = parameterReflection;
		auto& unsignedVariable = unsignedReflection.constantBuffers[0].variables[0];
		unsignedVariable.valueType = D3D_SVT_UINT;
		Engine::MaterialParameterLayout unsignedLayout;
		Engine::MaterialParameterSet emptyParameters;
		uint64_t unsignedLayoutHash = 0;
		for (bool isTexture : { false, true }) {
			unsignedVariable.isTexture = isTexture;
			unsignedLayout.Build(unsignedReflection);
			if (!isTexture) unsignedLayoutHash = unsignedLayout.GetContentHash();
			else if (unsignedLayoutHash == unsignedLayout.GetContentHash()) return false;
			auto bytes = Engine::MaterialParameterBufferBuilder::BuildElement(emptyParameters, emptyParameters, unsignedLayout, {});
			uint32_t value = 0;
			std::memcpy(&value, bytes.data(), sizeof(value));
			if (value != (isTexture ? UINT32_MAX : 0u)) return false;
		}
		Engine::MaterialParameterSet unsignedParameters;
		unsignedParameters.Set(graphParameterID, "Threshold", Engine::MaterialParameterSemantic::None,
			Engine::MaterialParameterValue{ .value = UINT32_MAX });
		unsignedVariable.isTexture = false;
		unsignedLayout.Build(unsignedReflection);
		const auto unsignedBytes = Engine::MaterialParameterBufferBuilder::BuildElement(unsignedParameters, emptyParameters, unsignedLayout, {});
		uint32_t unsignedValue = 0;
		std::memcpy(&unsignedValue, unsignedBytes.data(), sizeof(unsignedValue));
		if (unsignedValue != UINT32_MAX) return false;
		// 手書きShaderの標準TextureはMetadataなしでも未指定にできる
		for (const auto name : { Engine::MaterialParameterNames::BaseColorTexture, Engine::MaterialParameterNames::SpecularTexture }) {
			auto textureReflection = unsignedReflection;
			auto& variable = textureReflection.constantBuffers[0].variables[0];
			variable.name = name;
			variable.parameterID = Engine::MaterialParameterID::FromName(name);
			Engine::MaterialParameterLayout textureLayout;
			textureLayout.Build(textureReflection);
			const auto bytes = Engine::MaterialParameterBufferBuilder::BuildElement(emptyParameters, emptyParameters, textureLayout, {});
			uint32_t value = 0;
			std::memcpy(&value, bytes.data(), sizeof(value));
			if (value != UINT32_MAX) return false;
		}

		// 空Textureの上書きでは既定画像のGPU解決を要求しない
		Engine::GraphicsCore graphics;
		Engine::AssetDatabase database;
		Engine::MaterialAsset textureMaterial;
		textureMaterial.parameters.Set(Engine::MaterialParameterNames::BaseColorTexture,
			Engine::MaterialParameterValue{ .value = Engine::AssetID::New() });
		Engine::MaterialParameterSet textureOverride;
		textureOverride.Set(Engine::MaterialParameterNames::BaseColorTexture, emptyTexture);
		Engine::RaytracingMaterialResolver textureResolver;
		const auto textureData = textureResolver.BuildPrimitiveSubMeshData(graphics, database, textureMaterial,
			&textureOverride, Engine::Matrix4x4::Identity());
		if (textureData.baseColorTextureIndex != UINT32_MAX || textureResolver.HasPendingTextures()) return false;

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
		subMesh.visible = false;
		const Engine::SubMeshMaterial restoredSubMesh =
			nlohmann::json(subMesh).get<Engine::SubMeshMaterial>();
		// SubMeshの自己参照入力でも名前とMaterialの所有を維持する
		Engine::ECSWorld meshWorld(Engine::ECSWorldKind::Authoring);
		const Engine::Entity meshEntity = meshWorld.CreateEntity();
		std::array<Engine::SubMeshMaterial, 2> subMeshes;
		subMeshes[0].name = "First";
		subMeshes[1].name = "Second";
		subMeshes[0].stableID = Engine::UUID{ 41 };
		subMeshes[1].stableID = Engine::UUID{ 42 };
		subMeshes[1].materialInstance.Set("sampleCount", Engine::MaterialParameterValue{ .value = 17u });
		// SubMeshの並べ替えでも編集値と永続IDを同じ名前へ引き継ぐ
		subMeshes[1].sourceSubMeshIndex = 1;
		std::vector<Engine::MeshSubMeshLayoutItem> layout(2);
		layout[0].name = "Second";
		layout[0].sourceSubMeshIndex = 0;
		layout[1].name = "First";
		layout[1].sourceSubMeshIndex = 1;
		std::vector<Engine::SubMeshMaterial> reordered(subMeshes.begin(), subMeshes.end());
		if (!Engine::MeshSubMeshAuthoring::SyncComponentToLayout(layout, reordered, true) ||
			reordered[0].stableID != subMeshes[1].stableID || reordered[1].stableID != subMeshes[0].stableID ||
			reordered[0].materialInstance.GetContentHash() != subMeshes[1].materialInstance.GetContentHash()) return false;
		// 新しい先頭要素へ既存の名前付き要素を奪わせない
		layout[0].name = "Inserted";
		layout[1].name = "Second";
		reordered.assign(subMeshes.begin(), subMeshes.end());
		if (!Engine::MeshSubMeshAuthoring::SyncComponentToLayout(layout, reordered, true) ||
			reordered[1].stableID != subMeshes[1].stableID ||
			reordered[1].materialInstance.GetContentHash() != subMeshes[1].materialInstance.GetContentHash()) return false;
		Engine::SetMeshSubMeshes(meshWorld, meshEntity, subMeshes);
		Engine::SetMeshSubMeshes(meshWorld, meshEntity, Engine::GetMeshSubMeshes(meshWorld, meshEntity));
		Engine::SetMeshSubMeshes(meshWorld, meshEntity, Engine::GetMeshSubMeshes(meshWorld, meshEntity).subspan(1));
		const auto kept = Engine::GetMeshSubMeshes(meshWorld, meshEntity);
		if (kept.size() != 1 || kept[0].stableID != Engine::UUID{ 42 } || kept[0].name != "Second" ||
			kept[0].materialInstance.GetContentHash() != subMeshes[1].materialInstance.GetContentHash()) return false;
		return restoredSubMesh.surfaceMode ==
				Engine::MaterialSurfaceMode::Auto &&
			!restoredSubMesh.visible &&
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
}
