#include "TestContracts.h"
#include "TestFixtures.h"
#include "TestRunner.h"
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLookup.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderColorPrecisionPolicy.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingMaterialResolver.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetMetaStorage.h>
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

namespace {

	bool TestUnclippedMeshDepth() {

		using namespace Engine;
		MaterialParameterSet defaults;
		MaterialParameterSet overrides;
		const auto eligible = [&] { return MeshDrawPathCommon::HasUnclippedDepth(defaults, overrides); };
		if (eligible()) return false;
		defaults.Set(MaterialParameterNames::BaseColor, MaterialParameterValue{ .value = Color4{1.0f, 1.0f, 1.0f, 1.0f} });
		if (!eligible()) return false;
		// 境界上はclipされず、負のAlphaや非有限値は元のPixel Shaderで処理する
		for (const float alpha : {0.0f, 0.25f, 1.0f}) {
			overrides.Set(MaterialParameterNames::BaseColor, MaterialParameterValue{ .value = Vector4{1.0f, 1.0f, 1.0f, alpha} });
			overrides.Set(MaterialParameterNames::AlphaClip, MaterialParameterValue{ .value = alpha });
			if (!eligible()) return false;
			overrides.Set(MaterialParameterNames::AlphaClip, MaterialParameterValue{ .value = std::nextafter(alpha, 2.0f) });
			if (eligible()) return false;
		}
		overrides.clear();
		for (const float alpha : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
			overrides.Set(MaterialParameterNames::BaseColor, MaterialParameterValue{ .value = Color4{1.0f, 1.0f, 1.0f, alpha} });
			if (eligible()) return false;
		}
		overrides.clear();
		for (const float cutoff : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
			overrides.Set(MaterialParameterNames::AlphaClip, MaterialParameterValue{ .value = cutoff });
			if (eligible()) return false;
		}
		overrides.clear();
		// GPU未準備・存在しないIDも未割当にはしない。明示的な空の上書きだけ許可する
		for (const auto name : {MaterialParameterNames::BaseColorTexture, MaterialParameterNames::OpacityTexture}) {
			defaults.Set(name, MaterialParameterValue{ .value = AssetID{13, 17} });
			if (eligible()) return false;
			overrides.Set(name, MaterialParameterValue{ .value = AssetID{} });
			if (!eligible()) return false;
			overrides.Set(name, MaterialParameterValue{ .value = AssetID{19, 23} });
			if (eligible()) return false;
			overrides.Set(name, MaterialParameterValue{ .value = 0u });
			if (eligible()) return false;
			defaults.Set(name, MaterialParameterValue{ .value = AssetID{} });
			overrides.clear();
		}
		// 名前を変更した標準パラメータもバッファ構築と同じ優先順で解決する
		defaults.Set(MaterialParameterIDs::BaseColor, "Tint", MaterialParameterSemantic::BaseColor,
			MaterialParameterValue{ .value = Color4{1.0f, 1.0f, 1.0f, 1.0f} });
		overrides.Set("Tint", MaterialParameterValue{ .value = Color4{1.0f, 1.0f, 1.0f, -0.5f} });
		if (eligible()) return false;
		overrides.clear();
		if (!eligible()) return false;
		MeshGPUResource mesh;
		mesh.subMeshes.resize(1);
		mesh.subMeshes[0].hasBaseColorTexture = true;
		if (!MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(mesh, {}, 0)) return false;
		std::array<SubMeshMaterial, 1> subMeshes{};
		subMeshes[0].stableID = Engine::UUID{29};
		subMeshes[0].materialInstance.Set(MaterialParameterNames::BaseColorTexture,
			MaterialParameterValue{ .value = AssetID{} });
		if (MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(mesh, subMeshes, 0)) return false;
		PipelineVariantDesc source;
		source.shader = BuiltinAssets::Shaders::DefaultMeshPBRDepth;
		source.numRenderTargets = 0;
		PipelineVariantDesc depth = source;
		depth.shader = BuiltinAssets::Shaders::DefaultMeshZPrepass;
		if (!MeshDrawPathCommon::CanUsePositionOnlyDepthPipeline(source, depth)) return false;
		const PipelineVariantDesc compatible = depth;
		depth.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
		if (MeshDrawPathCommon::CanUsePositionOnlyDepthPipeline(source, depth)) return false;
		depth = compatible;
		depth.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		if (MeshDrawPathCommon::CanUsePositionOnlyDepthPipeline(source, depth)) return false;
		depth = compatible;
		depth.depthStencil.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
		if (MeshDrawPathCommon::CanUsePositionOnlyDepthPipeline(source, depth)) return false;
		depth = compatible;
		depth.shader = AssetID{37, 41};
		if (MeshDrawPathCommon::CanUsePositionOnlyDepthPipeline(source, depth)) return false;
		depth = compatible;
		depth.numRenderTargets = 1;
		if (MeshDrawPathCommon::CanUsePositionOnlyDepthPipeline(source, depth)) return false;
		// Sceneから直接参照されない最適化用Pipelineも製品収録し、Shader依存を維持する
		if (std::find(std::begin(BuiltinAssets::Runtime::Assets), std::end(BuiltinAssets::Runtime::Assets),
			BuiltinAssets::Pipelines::DefaultMeshZPrepass) == std::end(BuiltinAssets::Runtime::Assets)) return false;
		const auto root = RuntimePaths::GetEngineAssetPath("Rendering/Builtin/Mesh");
		AssetMeta pipelineMeta;
		AssetMeta shaderMeta;
		if (!AssetMetaStorage::ReadMetaFile(root / "DefaultMeshZPrepass/defaultMeshZPrepass.pipeline.json.meta", pipelineMeta) ||
			!AssetMetaStorage::ReadMetaFile(root / "DefaultMeshZPrepass/defaultMeshZPrepass.shader.json.meta", shaderMeta) ||
			pipelineMeta.guid != BuiltinAssets::Pipelines::DefaultMeshZPrepass ||
			shaderMeta.guid != BuiltinAssets::Shaders::DefaultMeshZPrepass) return false;
		RenderPipelineAsset sourceAsset;
		RenderPipelineAsset depthAsset;
		if (!FromJson(JsonFile::Load(root / "MeshPBR/meshPBRDepth.pipeline.json", false), sourceAsset) ||
			!FromJson(JsonFile::Load(root / "DefaultMeshZPrepass/defaultMeshZPrepass.pipeline.json", false), depthAsset) ||
			sourceAsset.variants.size() != depthAsset.variants.size() || depthAsset.variants.empty()) return false;
		for (size_t index = 0; index < depthAsset.variants.size(); ++index) {
			if (!MeshDrawPathCommon::CanUsePositionOnlyDepthPipeline(sourceAsset.variants[index], depthAsset.variants[index])) return false;
		}
		return true;
	}

	bool TestUnassignedMaterialTextures() {

		using namespace Engine;
		MaterialParameterSet defaults;
		MaterialParameterSet overrides;
		ShaderResourceBinding resource{};
		resource.name = "baseColorTexture";
		resource.parameterID = MaterialParameterIDs::BaseColorTexture;
		resource.semantic = MaterialParameterSemantic::BaseColorTexture;
		RootBindingLocation location{};
		location.kind = ShaderBindingKind::SRV;
		location.rawType = D3D_SIT_TEXTURE;
		location.space = 2;
		location.bindPoint = 0;
		const auto resolveMask = [&] {
			const MaterialParameterBinder::TextureBinding binding{
				.rootBinding = &location,
				.textureID = MaterialParameterLookup::ResolveTextureAsset(defaults, &overrides, resource),
				.semantic = resource.semantic };
			return MaterialParameterBinder::GetUnassignedTextureMask(std::span(&binding, 1));
		};
		if (resolveMask() != 1u) return false;
		// GPU未読込・欠損も空のIDとは区別し、従来の代替画像を使う
		defaults.Set(resource.name, MaterialParameterValue{ .value = AssetID{ 17, 19 } });
		if (resolveMask() != 0u) return false;
		overrides.Set(resource.name, MaterialParameterValue{ .value = AssetID{} });
		if (resolveMask() != 1u) return false;
		overrides.Set(resource.name, MaterialParameterValue{ .value = AssetID{ 23, 29 } });
		if (resolveMask() != 0u) return false;
		// 型が異なる上書きはTextureの既定値へ戻す
		overrides.Set(resource.name, MaterialParameterValue{ .value = 0.0f });
		if (resolveMask() != 0u) return false;
		// ShaderGraphの公開名を、生成された変数名ではなく安定IDへ対応付ける
		defaults = {};
		overrides = {};
		resource.parameterID = MaterialParameterID{ 12345 };
		resource.semantic = MaterialParameterSemantic::None;
		resource.name = "p_Texture_12345";
		defaults.Set(resource.parameterID, "画像", MaterialParameterSemantic::None,
			MaterialParameterValue{ .value = AssetID{ 17, 19 } });
		overrides.Set("画像", MaterialParameterValue{ .value = AssetID{} });
		if (MaterialParameterLookup::ResolveTextureAsset(defaults, &overrides, resource) || resolveMask() != 0u) return false;
		resource.semantic = MaterialParameterSemantic::BaseColorTexture;
		location.bindPoint = 6;
		if (resolveMask() != (1u << 6)) return false;
		location.space = 3;
		if (resolveMask() != 0u) return false;
		location.space = 2;
		location.bindPoint = 32;
		if (resolveMask() != 0u) return false;
		location.bindPoint = 0;
		location.bindCount = 0;
		if (resolveMask() != 0u) return false;
		location.bindCount = 1;
		resource.semantic = MaterialParameterSemantic::DisplacementTexture;
		if (resolveMask() != 0u) return false;
		RootBindingLocation aliasLocation = location;
		std::array<MaterialParameterBinder::TextureBinding, 2> aliases{
			MaterialParameterBinder::TextureBinding{ .rootBinding = &location, .semantic = MaterialParameterSemantic::BaseColorTexture },
			MaterialParameterBinder::TextureBinding{ .rootBinding = &aliasLocation, .textureID = AssetID{ 17, 19 },
				.semantic = MaterialParameterSemantic::BaseColorTexture } };
		if (MaterialParameterBinder::GetUnassignedTextureMask(aliases) != 0u) return false;
		aliases[1].textureID = {};
		if (MaterialParameterBinder::GetUnassignedTextureMask(aliases) != 1u) return false;
		for (const auto semantic : {MaterialParameterSemantic::None, MaterialParameterSemantic::DisplacementTexture}) {
			aliases[1].semantic = semantic;
			if (MaterialParameterBinder::GetUnassignedTextureMask(aliases) != 0u) return false;
		}
		aliases[1].semantic = MaterialParameterSemantic::BaseColorTexture;
		aliasLocation.rawType = D3D_SIT_STRUCTURED;
		if (MaterialParameterBinder::GetUnassignedTextureMask(aliases) != 0u) return false;
		aliasLocation.rawType = D3D_SIT_TEXTURE;
		aliasLocation.bindCount = 2;
		if (MaterialParameterBinder::GetUnassignedTextureMask(aliases) != 0u) return false;
		// 配列の先頭だけでなく途中のregisterと重なる場合も禁止する
		location.bindPoint = 1;
		if (MaterialParameterBinder::GetUnassignedTextureMask(aliases) != 0u) return false;
		location.bindPoint = 31;
		aliasLocation.bindCount = 0;
		if (MaterialParameterBinder::GetUnassignedTextureMask(aliases) != 0u) return false;
		aliasLocation.bindPoint = 31;
		aliasLocation.bindCount = 2;
		return MaterialParameterBinder::GetUnassignedTextureMask(aliases) == 0u;
	}

	bool TestMaterialDocumentIsolation() {

		using namespace Engine;
		// 型不正のMaterialで直前のデータを壊さない
		MaterialAsset retained = CreateDefaultMeshMaterialAsset("Retained");
		auto before = ToJson(retained);
		for (const auto& invalid : {nlohmann::json(42), nlohmann::json{{"name", 17}},
			nlohmann::json{{"shaderGraph", false}}, nlohmann::json{{"domain", 3}},
			nlohmann::json{{"renderState", {{"overridesRenderer", "wrong"}}}},
			nlohmann::json{{"passes", nlohmann::json::array({{{"passKind", 17}}})}}}) {

			if (FromJson(invalid, retained) || ToJson(retained) != before) {
				return false;
			}
		}
		MaterialParameterValue value{.value = 42u};
		if (ParseMaterialParameterValue(nlohmann::json{{"r", "wrong"}, {"g", 0}, {"b", 0}, {"a", 1}}, value) ||
			std::get<uint32_t>(value.value) != 42u) {
			return false;
		}
		// 破損したrecordが混ざっても正常な項目は読み込む
		MaterialInstanceParameters instance;
		ReadMaterialInstance(nlohmann::json::array({{{"name", 17}, {"value", 1}},
			{{"name", "speed"}, {"value", 2.0f}}, {{"name", "bad"}, {"semantic", 17}, {"value", 3}}}), instance);
		if (instance.size() != 1 || !instance.FindByName("speed")) {
			return false;
		}
		NEMTests::TestDirectory directory("MaterialSettings", RuntimePaths::GetGameAssetsRoot() / "Materials");
		auto path = directory.GetPath() / L"既定設定.json";
		auto pathText = Algorithm::PathToUTF8(path);
		DefaultMaterialSettings settings;
		AssetID mesh{1, 2}, sprite{3, 4};
		if (!settings.Load(pathText)) {
			return false;
		}
		settings.SetMesh(mesh);
		settings.SetSprite(sprite);
		if (!settings.Save()) {
			return false;
		}
		DefaultMaterialSettings restored;
		if (!restored.Load(pathText) || restored.GetMesh() != mesh || restored.GetSprite() != sprite) {
			return false;
		}
		auto document = JsonFile::Load(path, false);
		{
			// 保存失敗時に以前の設定ファイルを維持する
			NEMTests::TestFileReadLock lock(path);
			settings.SetMesh(AssetID{5, 6});
			if (settings.Save() || JsonFile::Load(path, false) != document) {
				return false;
			}
		}
		if (!JsonFile::Save(path, nlohmann::json{{"mesh", 42}}) || restored.Load(pathText) ||
			restored.GetMesh() != mesh || restored.GetSprite() != sprite) {
			return false;
		}
		// 別Projectの破損設定と未作成設定へ旧GUIDを持ち越さない
		auto malformed = directory.GetPath() / "Other.json";
		if (!JsonFile::Save(malformed, nlohmann::json{{"mesh", 42}}) ||
			restored.Load(Algorithm::PathToUTF8(malformed)) || restored.GetMesh() || restored.GetSprite()) {
			return false;
		}
		restored.SetMesh(mesh);
		if (!restored.Load(Algorithm::PathToUTF8(directory.GetPath() / "Missing.json")) || restored.GetMesh() ||
			restored.GetMeshOrBuiltin() != BuiltinAssets::Materials::DefaultMesh) {
			return false;
		}
		return true;
	}
}

namespace NEMTests {

	bool TestMaterialParameters() {

		if (!RunTest("TestUnclippedMeshDepth", TestUnclippedMeshDepth)) {
			return false;
		}
		if (!RunTest("TestUnassignedMaterialTextures", TestUnassignedMaterialTextures)) {
			return false;
		}

		if (!RunTest("TestMaterialDocumentIsolation", TestMaterialDocumentIsolation)) {
			return false;
		}
		// 頂点色と透明度が内部Albedoの範囲を越えるPrimitiveを除外する
		{
			using namespace Engine;
			PrimitiveRendererComponent primitive;
			const auto bounded = [&] { return RenderColorPrecisionPolicy::HasBoundedPrimitiveColors(primitive); };
			if (!bounded()) return false;
			primitive.type = PrimitiveType::Cylinder;
			if (!bounded()) return false;
			for (auto* color : {&primitive.cylinder.topColor, &primitive.cylinder.centerColor, &primitive.cylinder.bottomColor}) {
				color->r = 1.01f;
				if (bounded()) return false;
				*color = Color4::White();
				color->a = -0.1f;
				if (bounded()) return false;
				*color = Color4::White();
				color->b = std::numeric_limits<float>::quiet_NaN();
				if (bounded()) return false;
				*color = Color4::White();
			}
			for (const Color4 color : {Color4{16.01f, 1, 1, 1}, Color4{1, 1, 1, 1.01f}, Color4{1, 1, 1, -0.1f}}) {
				primitive.materialInstance.Set("color", MaterialParameterValue{.value = color});
				if (bounded()) return false;
			}
			primitive.materialInstance.Set("color", MaterialParameterValue{.value = Color4{16, 1, 1, 0.5f}});
			if (!bounded()) return false;
			primitive.blendMode = BlendMode::Add;
			if (bounded()) return false;
			primitive.blendMode = BlendMode::Normal;
			primitive.type = PrimitiveType::Plane;
			primitive.renderSpace = PrimitiveRenderSpace::Screen2D;
			if (bounded()) return false;
		}
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
		// 整数への変換は小数を切り捨て不正値をゼロで転送する
		const std::array<float, 9> floatValues{1.75f, -0.5f, -1.75f, -2147483648.0f, 2147483648.0f,
			4294967296.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
			-std::numeric_limits<float>::infinity()};
		const std::array<uint32_t, 9> signedBits{1u, 0u, static_cast<uint32_t>(-1), 0x80000000u, 0u, 0u, 0u, 0u, 0u};
		const std::array<uint32_t, 9> unsignedBits{1u, 0u, 0u, 0u, 0x80000000u, 0u, 0u, 0u, 0u};
		for (bool isSigned : {true, false}) {
			unsignedVariable.valueType = isSigned ? D3D_SVT_INT : D3D_SVT_UINT;
			unsignedLayout.Build(unsignedReflection);
			for (size_t index = 0; index < floatValues.size(); ++index) {
				unsignedParameters.Set(graphParameterID, "Threshold", Engine::MaterialParameterSemantic::None,
					Engine::MaterialParameterValue{.value = floatValues[index]});
				const auto bytes = Engine::MaterialParameterBufferBuilder::BuildElement(
					unsignedParameters, emptyParameters, unsignedLayout, {});
				uint32_t bits = 0;
				std::memcpy(&bits, bytes.data(), sizeof(bits));
				if (bits != (isSigned ? signedBits[index] : unsignedBits[index])) return false;
			}
		}
		int32_t retainedSigned = 42;
		uint32_t retainedUnsigned = 42;
		if (Math::TryConvertToInt32(2147483648.0f, retainedSigned) || retainedSigned != 42 ||
			Math::TryConvertToUInt32(-1.0f, retainedUnsigned) || retainedUnsigned != 42) return false;
		unsignedVariable.valueType = D3D_SVT_UINT;
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
