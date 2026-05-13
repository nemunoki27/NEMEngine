#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Core/Prefab/PrefabSystem.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/PrefabLinkComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Light/DirectionalLightComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/Graphics/Mesh/MeshSubMeshAuthoring.h>
#include <Engine/Core/Graphics/Texture/TextureAssetResolver.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>
#include <Engine/Editor/Command/Methods/InstantiatePrefabCommand.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Editor/Tools/EditorToolContext.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Utility/Json/JsonAdapter.h>
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Utility/ImGui/MyGUI.h>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

// windows
#include <windows.h>
#include <shellapi.h>

// c++
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <initializer_list>
#include <stack>
#include <string>
#include <system_error>
#include <vector>

//============================================================================
//	ProjectPanel classMethods
//============================================================================

namespace {

	constexpr const char* kProjectModelPreviewAtlasName = "ProjectPanelModelPreviewAtlas";
	constexpr uint32_t kModelPreviewColorTargetCount = 3;
	constexpr uint32_t kModelPreviewAssimpFlags =
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType;

	// アイテムの幅に基づいて利用可能な幅に収まる列数を計算する
	int32_t CalcGridColumnCount(float availableWidth, float itemWidth) {
		if (itemWidth <= 0.0f) {
			return 1;
		}
		return (std::max)(1, static_cast<int32_t>(availableWidth / itemWidth));
	}
	void HashCombine(uint64_t& seed, uint64_t value) {

		seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
	}
	void HashString(uint64_t& seed, const std::string& value) {

		for (char c : value) {
			HashCombine(seed, static_cast<uint8_t>(c));
		}
	}
	std::string GetMaterialTextureReference(aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) {

		if (!material) {
			return {};
		}
		for (aiTextureType type : textureTypes) {

			if (material->GetTextureCount(type) == 0) {
				continue;
			}

			aiString textureName;
			if (material->GetTexture(type, 0, &textureName) == AI_SUCCESS && 0 < textureName.length) {
				return textureName.C_Str();
			}
		}
		return {};
	}
	Engine::Vector3 ToEnginePreviewPosition(const aiVector3D& pos) {

		return Engine::Vector3(-pos.x, pos.y, pos.z);
	}
	void CollectAssimpNodePositions(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parentTransform,
		std::vector<Engine::Vector3>& outPositions) {

		(void)parentTransform;
		if (!scene || !node) {
			return;
		}

		for (uint32_t meshRefIndex = 0; meshRefIndex < node->mNumMeshes; ++meshRefIndex) {

			const uint32_t meshIndex = node->mMeshes[meshRefIndex];
			if (scene->mNumMeshes <= meshIndex) {
				continue;
			}
			const aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!mesh || mesh->mNumVertices == 0) {
				continue;
			}

			outPositions.reserve(outPositions.size() + mesh->mNumVertices);
			for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {

				outPositions.emplace_back(ToEnginePreviewPosition(mesh->mVertices[vertexIndex]));
			}
		}

		for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {

			CollectAssimpNodePositions(scene, node->mChildren[childIndex], aiMatrix4x4(), outPositions);
		}
	}
	float CalculatePreviewCameraDistance(const Engine::Vector3& min, const Engine::Vector3& max,
		const Engine::Vector3& center, float pitchDegrees, float yawDegrees, float fovYDegrees,
		float aspectRatio, float distanceScale) {

		const float halfFovY = (fovYDegrees * 0.5f) * 3.1415926535f / 180.0f;
		const float tanY = (std::max)(std::tan(halfFovY), 0.001f);
		const float tanX = (std::max)(tanY * (std::max)(aspectRatio, 0.001f), 0.001f);

		const Engine::Matrix4x4 rotation = Engine::Matrix4x4::MakeRotateMatrix(
			Engine::Vector3(pitchDegrees, yawDegrees, 0.0f));
		const Engine::Matrix4x4 inverseRotation = Engine::Matrix4x4::Inverse(rotation);

		float requiredDistance = 0.1f;
		for (int32_t ix = 0; ix < 2; ++ix) {
			for (int32_t iy = 0; iy < 2; ++iy) {
				for (int32_t iz = 0; iz < 2; ++iz) {

					const Engine::Vector3 corner(
						ix == 0 ? min.x : max.x,
						iy == 0 ? min.y : max.y,
						iz == 0 ? min.z : max.z);
					const Engine::Vector3 local = Engine::Vector3::TransferNormal(corner - center, inverseRotation);
					requiredDistance = (std::max)(requiredDistance, std::fabs(local.x) / tanX - local.z);
					requiredDistance = (std::max)(requiredDistance, std::fabs(local.y) / tanY - local.z);
				}
			}
		}
		return (std::max)(requiredDistance, 0.1f) * (std::max)(distanceScale, 1.0f) * 1.08f;
	}
	void ImportModelReferencedTextures(Engine::AssetDatabase& database, Engine::AssetID meshAssetID) {

		if (!meshAssetID) {
			return;
		}

		const std::filesystem::path fullPath = database.ResolveFullPath(meshAssetID);
		if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
			return;
		}

		Engine::TextureAssetResolver textureResolver{};
		textureResolver.Build(fullPath);

		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(fullPath.string(), kModelPreviewAssimpFlags);
		if (!scene || scene->mNumMaterials == 0) {
			return;
		}

		auto importTexture = [&](const std::string& reference) {

			const std::string assetPath = textureResolver.ResolveAssetPath(reference);
			if (!assetPath.empty()) {

				database.ImportOrGet(assetPath, Engine::AssetType::Texture);
			}
			};

		for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {

			aiMaterial* material = scene->mMaterials[materialIndex];
			importTexture(GetMaterialTextureReference(material, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_SPECULAR }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }));
			importTexture(GetMaterialTextureReference(material, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }));
		}
	}
	// ドラッグ&ドロップのソースを描画する
	void DrawAssetDragDropSource(const Engine::ProjectAssetEntry& asset,
		ImGuiDragDropFlags flags = ImGuiDragDropFlags_None) {

		if (ImGui::BeginDragDropSource(flags)) {

			Engine::EditorAssetDragDropPayload payload{};
			payload.assetID = asset.assetID;
			payload.assetType = asset.type;
			payload.isDirectory = 0;
			strncpy_s(payload.assetPath, asset.assetPath.c_str(), sizeof(payload.assetPath) - 1);

			ImGui::SetDragDropPayload(Engine::IEditorPanel::kProjectAssetDragDropPayloadType,
				&payload, sizeof(payload));

			ImGui::TextUnformatted(asset.displayName.c_str());
			ImGui::TextDisabled("%s", asset.assetPath.c_str());

			ImGui::EndDragDropSource();
		}
	}
	// Project内のファイル/フォルダ移動用ドラッグソースを描画する
	void DrawProjectFileMoveSource(const std::string& virtualPath, bool isDirectory, const char* displayName) {

		if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			return;
		}

		Engine::EditorAssetDragDropPayload payload{};
		payload.assetType = Engine::AssetType::Unknown;
		payload.isDirectory = isDirectory ? 1 : 0;
		strncpy_s(payload.assetPath, virtualPath.c_str(), sizeof(payload.assetPath) - 1);

		ImGui::SetDragDropPayload(Engine::IEditorPanel::kProjectAssetDragDropPayloadType, &payload, sizeof(payload));
		ImGui::TextUnformatted(displayName);
		ImGui::TextDisabled("%s", virtualPath.c_str());
		ImGui::EndDragDropSource();
	}
	// 仮想パスを'/'または'\'で分割して、ディレクトリ名のリストを返す
	std::vector<std::string> SplitVirtualPath(const std::string& path) {

		std::vector<std::string> result;
		std::string current;

		for (char c : path) {
			if (c == '/' || c == '\\') {
				if (!current.empty()) {
					result.emplace_back(std::move(current));
					current.clear();
				}
			} else {
				current.push_back(c);
			}
		}
		if (!current.empty()) {
			result.emplace_back(std::move(current));
		}
		return result;
	}
	// 仮想パスを分割して、インデックスから対応するディレクトリノードのリストを構築する
	std::vector<const Engine::ProjectDirectoryNode*> BuildBreadcrumbTrail(
		const Engine::ProjectAssetIndex& index,
		const std::string& selectedDirectory) {

		std::vector<const Engine::ProjectDirectoryNode*> trail;

		const Engine::ProjectDirectoryNode& root = index.GetRoot();
		trail.push_back(&root);

		if (selectedDirectory == root.virtualPath) {
			return trail;
		}

		std::string currentPath = root.virtualPath;
		const std::string prefix = root.virtualPath + "/";
		if (selectedDirectory.rfind(prefix, 0) != 0) {
			return trail;
		}

		const std::vector<std::string> parts = SplitVirtualPath(selectedDirectory.substr(prefix.size()));
		for (const std::string& part : parts) {

			currentPath += "/" + part;

			if (const auto* node = index.FindDirectory(currentPath)) {
				trail.push_back(node);
			} else {
				break;
			}
		}

		return trail;
	}

	// ShellExecuteWで対象を開く
	bool OpenWithShell(const std::filesystem::path& target,
		const std::wstring& parameters = std::wstring{},
		const std::filesystem::path& workingDirectory = std::filesystem::path{}) {

		if (target.empty()) {
			return false;
		}

		const wchar_t* parameterText = parameters.empty() ? nullptr : parameters.c_str();
		const wchar_t* workingDirectoryText = workingDirectory.empty() ? nullptr : workingDirectory.c_str();
		const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", target.c_str(), parameterText, workingDirectoryText, SW_SHOWNORMAL);
		return reinterpret_cast<INT_PTR>(result) > 32;
	}
	// 環境変数からパスを取得する
	std::filesystem::path GetEnvironmentPath(const char* name) {

		char* value = nullptr;
		size_t valueLength = 0;
		if (_dupenv_s(&value, &valueLength, name) != 0 || value == nullptr) {
			return {};
		}

		std::filesystem::path result = value;
		std::free(value);
		return result;
	}
	// インストールされているVisual Studioのdevenv.exeを探す
	std::filesystem::path FindVisualStudioExecutable() {

		const std::array<std::filesystem::path, 2> roots = {
			GetEnvironmentPath("ProgramFiles"),
			GetEnvironmentPath("ProgramFiles(x86)"),
		};
		constexpr std::array<const char*, 3> versions = { "18", "17", "16" };
		constexpr std::array<const char*, 4> editions = { "Community", "Professional", "Enterprise", "Preview" };

		for (const auto& root : roots) {
			if (root.empty()) {
				continue;
			}
			for (const char* version : versions) {
				for (const char* edition : editions) {

					const std::filesystem::path devenv =
						root / "Microsoft Visual Studio" / version / edition / "Common7/IDE/devenv.exe";
					std::error_code ec;
					if (std::filesystem::exists(devenv, ec) && !ec) {
						return devenv;
					}
				}
			}
		}
		return {};
	}
	// ProjectPanelの表示状態を保存するパスを返す
	std::filesystem::path GetProjectPanelStatePath() {

		return Engine::RuntimePaths::GetEngineAssetPath("Config/projectPanel.exeConfig.json");
	}

	// ScriptアセットをVisual Studioで開く
	bool OpenScriptAssetInVisualStudio(const Engine::ProjectAssetEntry& asset) {

		const std::filesystem::path scriptPath = Engine::RuntimePaths::ResolveAssetPath(asset.assetPath);
		std::error_code scriptEc;
		if (scriptPath.empty() || !std::filesystem::exists(scriptPath, scriptEc) || scriptEc) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"ProjectPanel: script file was not found. path={}", asset.assetPath);
			return false;
		}

		const std::filesystem::path workingDirectory = Engine::RuntimePaths::GetProjectRoot();
		const std::filesystem::path visualStudio = FindVisualStudioExecutable();
		if (!visualStudio.empty()) {

			const std::wstring parameters = L"/Edit \"" + scriptPath.wstring() + L"\"";
			if (OpenWithShell(visualStudio, parameters, workingDirectory)) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::info,
					"ProjectPanel: opened script in Visual Studio. path={}", scriptPath.string());
				return true;
			}
		}

		if (OpenWithShell(scriptPath, std::wstring{}, workingDirectory)) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::info,
				"ProjectPanel: opened script via shell association because Visual Studio was not found. path={}", scriptPath.string());
			return true;
		}

		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
			"ProjectPanel: failed to open Visual Studio for script asset.");
		return false;
	}

	// Prefab保存元のEntityツリーへPrefabLinkを設定する
	void AttachPrefabLinkToSourceTree(Engine::ECSWorld& world, const Engine::Entity& root, Engine::AssetID prefabAsset) {

		if (!world.IsAlive(root) || !prefabAsset) {
			return;
		}

		// 同じPrefab化操作で作られたEntity群をまとめるID
		const Engine::UUID prefabInstanceID = Engine::UUID::New();

		std::stack<Engine::Entity> stack;
		stack.push(root);
		while (!stack.empty()) {

			const Engine::Entity entity = stack.top();
			stack.pop();

			if (!world.IsAlive(entity) || !world.HasComponent<Engine::SceneObjectComponent>(entity)) {
				continue;
			}

			const auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
			auto& prefabLink = world.HasComponent<Engine::PrefabLinkComponent>(entity) ?
				world.GetComponent<Engine::PrefabLinkComponent>(entity) :
				world.AddComponent<Engine::PrefabLinkComponent>(entity);

			// Prefabファイル内のLocalFileIDは保存時点のSceneObject.localFileIDと同じ値を使う
			prefabLink.prefabAsset = prefabAsset;
			prefabLink.prefabLocalFileID = sceneObject.localFileID;
			prefabLink.prefabInstanceID = prefabInstanceID;
			prefabLink.isPrefabRoot = entity == root;

			if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
				continue;
			}

			Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(entity).firstChild;
			while (child.IsValid() && world.IsAlive(child)) {

				stack.push(child);
				if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
					break;
				}
				child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
			}
		}
	}
}

Engine::ProjectPanel::ProjectPanel(TextureUploadService& textureUploadService) {

	thumbnailCache_.Init(textureUploadService);
	LoadPersistentState();
	dirty_ = true;
}

Engine::ProjectPanel::~ProjectPanel() {

	SavePersistentState();
}

void Engine::ProjectPanel::Rebuild(AssetDatabase& database) {

	// インデックスとサムネイルキャッシュを再構築する
	database.RebuildMeta();
	assetIndex_.Rebuild(database, assetSource_);
	if (!assetIndex_.FindDirectory(selectedDirectory_)) {
		selectedDirectory_ = assetIndex_.GetRoot().virtualPath;
	}
	dirty_ = false;
}

void Engine::ProjectPanel::Draw(const EditorPanelContext& context) {

	// プロジェクトパネルの表示状態を確認
	if (!context.layoutState->showProject) {
		return;
	}

	const bool wasOpen = context.layoutState->showProject;
	if (!ImGui::Begin("Project", &context.layoutState->showProject)) {
		ImGui::End();
		if (wasOpen && !context.layoutState->showProject) {

			SavePersistentState();
		}
		return;
	}

	// アセットデータベースが利用できない場合はエラーメッセージを表示して終了
	if (!context.editorContext || !context.editorContext->assetDatabase) {
		ImGui::TextDisabled("AssetDatabase is not available.");
		ImGui::End();
		return;
	}

	// 変更された場合のインデックスの再構築
	AssetDatabase& database = *context.editorContext->assetDatabase;
	if (dirty_) {
		Rebuild(database);
	}

	ImGui::SetWindowFontScale(0.8f);
	DrawSourceSelector(context, database);
	DrawHeader(context, database);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();

	ImGui::SameLine();

	// ディレクトリの内容を描画
	if (ImGui::BeginChild("##ProjectContent", ImVec2(0.0f, 0.0f), true)) {
		if (const ProjectDirectoryNode* node = assetIndex_.FindDirectory(selectedDirectory_)) {

			DrawDirectoryContents(context, database, *node);
		}
	}
	ImGui::EndChild();

	DrawCreateAssetPopup(database);
	DrawRenameAssetPopup(database);
	ApplyPendingFileOperationRefresh(database);

	ImGui::End();
	if (wasOpen && !context.layoutState->showProject) {

		SavePersistentState();
	}

	if (context.layoutState->showProject && showModelPreviewSettingsWindow_) {

		DrawModelPreviewSettingsWindow();
	}
}

void Engine::ProjectPanel::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	// ProjectPanelはToolPanel上の独立ウィンドウを持たず、RenderTexture作成機能だけを利用する。
}

void Engine::ProjectPanel::DrawModelPreviewSettingsWindow() {

	const bool wasOpen = showModelPreviewSettingsWindow_;
	ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("モデルプレビュー設定", &showModelPreviewSettingsWindow_)) {

		ImGui::End();
		if (wasOpen && !showModelPreviewSettingsWindow_) {

			SavePersistentState();
		}
		return;
	}

	bool changed = false;
	bool saveRequested = false;
	bool atlasRebuildRequested = false;
	auto consumeEditState = [&](bool valueChanged, bool rebuildAtlasOnCommit) {

		changed |= valueChanged;
		if (ImGui::IsItemDeactivatedAfterEdit()) {

			saveRequested = true;
			atlasRebuildRequested |= rebuildAtlasOnCommit;
		}
	};

	ImGui::TextUnformatted("アトラス");
	consumeEditState(ImGui::DragInt("スロットサイズ", &modelPreviewSettings_.tileSize, 1.0f, 64, 512), true);

	float clearColor[4] = {
		modelPreviewSettings_.clearColor.r,
		modelPreviewSettings_.clearColor.g,
		modelPreviewSettings_.clearColor.b,
		modelPreviewSettings_.clearColor.a,
	};
	const bool clearColorChanged = ImGui::ColorEdit4("背景色", clearColor);
	if (clearColorChanged) {

		modelPreviewSettings_.clearColor = Color4(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
	}
	consumeEditState(clearColorChanged, true);

	ImGui::Separator();
	ImGui::TextUnformatted("カメラ");
	consumeEditState(ImGui::DragFloat("視野角", &modelPreviewSettings_.cameraFovY, 0.1f, 10.0f, 80.0f, "%.1f"), false);
	consumeEditState(ImGui::DragFloat("距離倍率", &modelPreviewSettings_.cameraDistanceScale, 0.01f, 0.5f, 6.0f, "%.2f"), false);
	consumeEditState(ImGui::DragFloat("ピッチ角", &modelPreviewSettings_.cameraPitchDegrees, 0.1f, -45.0f, 45.0f, "%.1f"), false);
	consumeEditState(ImGui::DragFloat("ヨー角", &modelPreviewSettings_.cameraYawDegrees, 0.1f, -360.0f, 360.0f, "%.1f"), false);

	ImGui::Separator();
	ImGui::TextUnformatted("ライト");
	float lightDirection[3] = {
		modelPreviewSettings_.lightDirection.x,
		modelPreviewSettings_.lightDirection.y,
		modelPreviewSettings_.lightDirection.z,
	};
	const bool lightDirectionChanged = ImGui::DragFloat3("ライト方向", lightDirection, 0.01f, -1.0f, 1.0f, "%.2f");
	if (lightDirectionChanged) {

		modelPreviewSettings_.lightDirection = Vector3(lightDirection[0], lightDirection[1], lightDirection[2]);
	}
	consumeEditState(lightDirectionChanged, false);
	consumeEditState(ImGui::DragFloat("ライト強度", &modelPreviewSettings_.lightIntensity, 0.01f, 0.0f, 10.0f, "%.2f"), false);

	ImGui::Separator();
	if (ImGui::Button("初期値に戻す")) {

		modelPreviewSettings_ = ModelPreviewAppearanceSettings{};
		changed = true;
		saveRequested = true;
		atlasRebuildRequested = true;
	}

	if (changed) {

		ClampModelPreviewSettings();
	}
	if (atlasRebuildRequested) {

		InvalidateModelPreviewAtlas();
	}
	if (saveRequested) {

		SavePersistentState();
	}

	ImGui::End();
	if (wasOpen && !showModelPreviewSettingsWindow_) {

		SavePersistentState();
	}
}

void Engine::ProjectPanel::ClampModelPreviewSettings() {

	modelPreviewSettings_.tileSize = std::clamp(modelPreviewSettings_.tileSize, 64, 512);
	modelPreviewSettings_.cameraFovY = std::clamp(modelPreviewSettings_.cameraFovY, 10.0f, 80.0f);
	modelPreviewSettings_.cameraDistanceScale = std::clamp(modelPreviewSettings_.cameraDistanceScale, 0.5f, 6.0f);
	modelPreviewSettings_.cameraPitchDegrees = std::clamp(modelPreviewSettings_.cameraPitchDegrees, -45.0f, 45.0f);
	modelPreviewSettings_.lightIntensity = std::clamp(modelPreviewSettings_.lightIntensity, 0.0f, 10.0f);
	if (modelPreviewSettings_.lightDirection.Length() <= 0.001f) {

		modelPreviewSettings_.lightDirection = ModelPreviewAppearanceSettings{}.lightDirection;
	}
}

void Engine::ProjectPanel::ApplyModelPreviewLightSettings() {

	if (!modelPreviewWorld_ || !modelPreviewWorld_->IsAlive(modelPreviewLightEntity_)) {
		return;
	}

	DirectionalLightComponent* light = modelPreviewWorld_->TryGetComponent<DirectionalLightComponent>(modelPreviewLightEntity_);
	if (!light) {
		return;
	}

	light->direction = modelPreviewSettings_.lightDirection.Normalize();
	light->intensity = modelPreviewSettings_.lightIntensity;
}

void Engine::ProjectPanel::InvalidateModelPreviewAtlas() {

	modelPreviewWorld_.reset();
	modelPreviewLightEntity_ = Entity::Null();
	modelPreviewSlots_.clear();
	modelPreviewSlotByAsset_.clear();
	modelPreviewDirectory_.clear();
	modelPreviewSignature_ = 0;
	modelPreviewAtlasSize_ = Vector2I{};
	modelPreviewAtlasRebuildRequested_ = true;
}

void Engine::ProjectPanel::PrepareModelPreviewAtlas(const EditorPanelContext& context,
	AssetDatabase& database, const ProjectDirectoryNode& node) {

	std::vector<const ProjectAssetEntry*> meshAssets{};
	meshAssets.reserve(node.assets.size());
	for (const auto& asset : node.assets) {
		if (asset.type == AssetType::Mesh) {
			meshAssets.emplace_back(&asset);
		}
	}

	if (meshAssets.empty()) {
		modelPreviewSlots_.clear();
		modelPreviewSlotByAsset_.clear();
		modelPreviewSignature_ = 0;
		modelPreviewDirectory_ = node.virtualPath;
		modelPreviewLightEntity_ = Entity::Null();
		return;
	}

	const uint64_t signature = BuildModelPreviewSignature(node, meshAssets);
	if (signature != modelPreviewSignature_ || modelPreviewDirectory_ != node.virtualPath) {
		RebuildModelPreviewSlots(database, node, meshAssets, signature);
	}

	if (!context.graphicsCore || !context.renderPipeline || !modelPreviewWorld_ ||
		modelPreviewSlots_.empty() || modelPreviewAtlasSize_.x <= 0 || modelPreviewAtlasSize_.y <= 0) {
		return;
	}

	EditorToolContext toolContext{};
	toolContext.panelContext = &context;
	toolContext.toolContext.world = context.editorContext ? context.editorContext->activeWorld : nullptr;
	toolContext.toolContext.assetDatabase = &database;
	toolContext.toolContext.activeSceneHeader = context.editorContext ? context.editorContext->activeSceneHeader : nullptr;
	toolContext.toolContext.activeSceneAsset = context.editorContext ? context.editorContext->activeSceneAsset : AssetID{};
	toolContext.toolContext.activeSceneInstanceID = context.editorContext ? context.editorContext->activeSceneInstanceID : UUID{};
	if (context.editorContext) {
		toolContext.toolContext.activeScenePath = context.editorContext->activeScenePath;
	}
	toolContext.toolContext.isPlaying = context.IsPlaying();
	toolContext.toolContext.canEditScene = context.CanEditScene();

	BeginEditorToolFrame(toolContext);

	EditorToolRenderTexture* atlas = FindRenderTexture(kProjectModelPreviewAtlasName);
	if (atlas && (atlas->size.x != modelPreviewAtlasSize_.x ||
		atlas->size.y != modelPreviewAtlasSize_.y ||
		(modelPreviewAtlasRebuildRequested_ && atlas->clearColor != modelPreviewSettings_.clearColor))) {

		DestroyRenderTexture(kProjectModelPreviewAtlasName);
		atlas = nullptr;
	}
	if (!atlas) {
		atlas = CreateRenderTexture(kProjectModelPreviewAtlasName,
			modelPreviewAtlasSize_, modelPreviewSettings_.clearColor, kModelPreviewColorTargetCount);
	}
	if (atlas) {
		RenderModelPreviewAtlas(toolContext, *atlas);
		modelPreviewAtlasRebuildRequested_ = false;
	}

	EndEditorToolFrame();
}

void Engine::ProjectPanel::RebuildModelPreviewSlots(AssetDatabase& database, const ProjectDirectoryNode& node,
	const std::vector<const ProjectAssetEntry*>& meshAssets, uint64_t signature) {

	modelPreviewWorld_ = std::make_unique<ECSWorld>();
	modelPreviewSlots_.clear();
	modelPreviewSlotByAsset_.clear();
	modelPreviewDirectory_ = node.virtualPath;
	modelPreviewSignature_ = signature;

	const int32_t count = static_cast<int32_t>(meshAssets.size());
	const int32_t columns = (std::max)(1, static_cast<int32_t>(std::ceil(std::sqrt(static_cast<float>(count)))));
	const int32_t rows = (std::max)(1, (count + columns - 1) / columns);
	const int32_t tileSize = modelPreviewSettings_.tileSize;
	modelPreviewAtlasSize_ = Vector2I(columns * tileSize, rows * tileSize);

	Entity lightEntity = modelPreviewWorld_->CreateEntity(UUID::New());
	auto& lightTransform = modelPreviewWorld_->AddComponent<TransformComponent>(lightEntity);
	lightTransform.worldMatrix = Matrix4x4::Identity();
	lightTransform.isDirty = false;
	auto& light = modelPreviewWorld_->AddComponent<DirectionalLightComponent>(lightEntity);
	light.direction = modelPreviewSettings_.lightDirection.Normalize();
	light.intensity = modelPreviewSettings_.lightIntensity;
	modelPreviewLightEntity_ = lightEntity;

	modelPreviewSlots_.reserve(meshAssets.size());
	for (int32_t i = 0; i < count; ++i) {
		const ProjectAssetEntry& asset = *meshAssets[static_cast<size_t>(i)];
		ImportModelReferencedTextures(database, asset.assetID);

		Entity entity = modelPreviewWorld_->CreateEntity(UUID::New());
		auto& transform = modelPreviewWorld_->AddComponent<TransformComponent>(entity);
		transform.worldMatrix = Matrix4x4::Identity();
		transform.isDirty = false;

		auto& renderer = modelPreviewWorld_->AddComponent<MeshRendererComponent>(entity);
		renderer.mesh = asset.assetID;
		renderer.material = {};
		renderer.queue = "Opaque";
		renderer.visible = true;
		renderer.enableZPrepass = true;
		MeshSubMeshAuthoring::SyncComponent(&database, renderer, false);

		const int32_t column = i % columns;
		const int32_t row = i / columns;
		const Vector2I pixelPos(column * tileSize, row * tileSize);
		const Vector2I pixelSize(tileSize, tileSize);

		ModelPreviewSlot slot{};
		slot.assetID = asset.assetID;
		slot.assetPath = asset.assetPath;
		slot.entity = entity;
		slot.pixelPos = pixelPos;
		slot.pixelSize = pixelSize;
		slot.uv0 = ImVec2(
			static_cast<float>(pixelPos.x) / static_cast<float>(modelPreviewAtlasSize_.x),
			static_cast<float>(pixelPos.y) / static_cast<float>(modelPreviewAtlasSize_.y));
		slot.uv1 = ImVec2(
			static_cast<float>(pixelPos.x + pixelSize.x) / static_cast<float>(modelPreviewAtlasSize_.x),
			static_cast<float>(pixelPos.y + pixelSize.y) / static_cast<float>(modelPreviewAtlasSize_.y));
		slot.bounds = ComputeModelPreviewBounds(database, asset.assetID);

		modelPreviewSlotByAsset_[slot.assetID] = modelPreviewSlots_.size();
		modelPreviewSlots_.emplace_back(std::move(slot));
	}
}

void Engine::ProjectPanel::RenderModelPreviewAtlas(const EditorToolContext& toolContext,
	EditorToolRenderTexture& atlas) {

	if (!toolContext.panelContext || !toolContext.panelContext->renderPipeline || !modelPreviewWorld_) {
		return;
	}

	ApplyModelPreviewLightSettings();
	RenderToTexture(atlas, [&](EditorToolRenderContext& renderContext) {

		for (const ModelPreviewSlot& slot : modelPreviewSlots_) {
			if (!modelPreviewWorld_->IsAlive(slot.entity)) {
				continue;
			}

			EntityPreviewRenderRequest request{};
			request.world = modelPreviewWorld_.get();
			request.systemContext = toolContext.toolContext.systemContext;
			request.assetDatabase = toolContext.toolContext.assetDatabase;
			request.sceneHeader = toolContext.toolContext.activeSceneHeader;
			request.sceneInstanceID = {};
			request.rootEntity = slot.entity;
			request.surface = atlas.GetRenderTarget();
			request.camera = BuildModelPreviewCamera(slot.bounds);
			request.clearColor = atlas.clearColor;
			request.clearSurface = false;
			request.useViewportRect = true;
			request.viewportX = static_cast<uint32_t>(slot.pixelPos.x);
			request.viewportY = static_cast<uint32_t>(slot.pixelPos.y);
			request.viewportWidth = static_cast<uint32_t>(slot.pixelSize.x);
			request.viewportHeight = static_cast<uint32_t>(slot.pixelSize.y);

			toolContext.panelContext->renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}
		}, atlas.clearColor);
}

bool Engine::ProjectPanel::TryGetModelPreviewImage(AssetID assetID,
	ImTextureID& outTextureID, ImVec2& outUV0, ImVec2& outUV1) const {

	const auto it = modelPreviewSlotByAsset_.find(assetID);
	if (it == modelPreviewSlotByAsset_.end() || modelPreviewSlots_.size() <= it->second) {
		return false;
	}

	const EditorToolRenderTexture* atlas = FindRenderTexture(kProjectModelPreviewAtlasName);
	if (!atlas || !atlas->IsValid()) {
		return false;
	}

	outTextureID = atlas->GetImTextureID();
	if (outTextureID == static_cast<ImTextureID>(0)) {
		return false;
	}

	const ModelPreviewSlot& slot = modelPreviewSlots_[it->second];
	outUV0 = slot.uv0;
	outUV1 = slot.uv1;
	return true;
}

uint64_t Engine::ProjectPanel::BuildModelPreviewSignature(const ProjectDirectoryNode& node,
	const std::vector<const ProjectAssetEntry*>& meshAssets) const {

	uint64_t signature = 1469598103934665603ull;
	HashString(signature, node.virtualPath);
	HashCombine(signature, static_cast<uint64_t>(meshAssets.size()));
	for (const ProjectAssetEntry* asset : meshAssets) {
		if (!asset) {
			continue;
		}
		HashCombine(signature, asset->assetID.value);
		HashString(signature, asset->assetPath);
	}
	return signature;
}

Engine::ProjectPanel::ModelPreviewBounds Engine::ProjectPanel::ComputeModelPreviewBounds(
	AssetDatabase& database, AssetID meshAssetID) const {

	ModelPreviewBounds bounds{};
	if (!meshAssetID) {
		return bounds;
	}

	const std::filesystem::path fullPath = database.ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return bounds;
	}

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath.string(), kModelPreviewAssimpFlags);
	if (!scene || !scene->HasMeshes()) {
		return bounds;
	}

	std::vector<Vector3> positions{};
	CollectAssimpNodePositions(scene, scene->mRootNode, aiMatrix4x4(), positions);
	if (positions.empty()) {
		return bounds;
	}

	Vector3 minV(FLT_MAX, FLT_MAX, FLT_MAX);
	Vector3 maxV(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const Vector3& p : positions) {

		minV.x = (std::min)(minV.x, p.x);
		minV.y = (std::min)(minV.y, p.y);
		minV.z = (std::min)(minV.z, p.z);
		maxV.x = (std::max)(maxV.x, p.x);
		maxV.y = (std::max)(maxV.y, p.y);
		maxV.z = (std::max)(maxV.z, p.z);
	}
	bounds.min = minV;
	bounds.max = maxV;
	bounds.center = (minV + maxV) * 0.5f;

	float radius = 0.0f;
	for (const Vector3& p : positions) {

		radius = (std::max)(radius, (p - bounds.center).Length());
	}
	bounds.radius = (std::max)(radius, 0.1f);
	bounds.valid = true;
	return bounds;
}

Engine::ManualRenderCameraState Engine::ProjectPanel::BuildModelPreviewCamera(
	const ModelPreviewBounds& bounds) const {

	const Vector3 center = bounds.valid ? bounds.center : Vector3::AnyInit(0.0f);
	const float radius = (std::max)(bounds.valid ? bounds.radius : 1.0f, 0.1f);
	const float fovY = modelPreviewSettings_.cameraFovY;
	const float pitchDegrees = modelPreviewSettings_.cameraPitchDegrees;
	const float yawDegrees = modelPreviewSettings_.cameraYawDegrees;
	const float distance = bounds.valid ?
		CalculatePreviewCameraDistance(bounds.min, bounds.max, center, pitchDegrees, yawDegrees, fovY,
			1.0f, modelPreviewSettings_.cameraDistanceScale) :
		radius * modelPreviewSettings_.cameraDistanceScale;
	const Matrix4x4 cameraRotation = Matrix4x4::MakeRotateMatrix(Vector3(pitchDegrees, yawDegrees, 0.0f));
	const Vector3 cameraForward(cameraRotation.m[2][0], cameraRotation.m[2][1], cameraRotation.m[2][2]);

	ManualRenderCameraState camera{};
	camera.enableOrthographic = false;
	camera.enablePerspective = true;
	camera.perspectiveFovY = fovY;
	camera.perspectiveNearClip = 0.01f;
	camera.perspectiveFarClip = (std::max)(10000.0f, distance + radius * 4.0f);
	camera.transform3D.pos = center - cameraForward * distance;
	camera.transform3D.rotation = Vector3(pitchDegrees, yawDegrees, 0.0f);
	return camera;
}

void Engine::ProjectPanel::DrawHeader([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	// ルートへ戻る
	if (ImGui::Button(GetSourceRootPath())) {
		selectedDirectory_ = assetIndex_.GetRoot().virtualPath;
		selectedAsset_ = {};
	}
	DrawProjectItemMoveDropTarget(database, assetIndex_.GetRoot().virtualPath);

	const auto trail = BuildBreadcrumbTrail(assetIndex_, selectedDirectory_);

	// 中間階層はボタン
	for (size_t i = 1; i + 1 < trail.size(); ++i) {

		ImGui::SameLine();
		ImGui::TextUnformatted(">");
		ImGui::SameLine();

		if (ImGui::Button(trail[i]->name.c_str())) {
			selectedDirectory_ = trail[i]->virtualPath;
			selectedAsset_ = {};
		}
		DrawProjectItemMoveDropTarget(database, trail[i]->virtualPath);
	}

	// 現在階層はテキスト
	ImGui::SameLine();
	ImGui::TextUnformatted(">");
	ImGui::SameLine();

	const char* currentName = trail.empty() ? GetSourceRootPath() : trail.back()->name.c_str();
	ImGui::TextUnformatted(currentName);

	// 右端に Refresh
	const char* label = "Refresh";
	float buttonWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const char* createLabel = "Create";
	float createButtonWidth = ImGui::CalcTextSize(createLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const char* previewSettingsLabel = "プレビュー設定";
	float previewSettingsButtonWidth =
		ImGui::CalcTextSize(previewSettingsLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;

	float rightX = ImGui::GetWindowContentRegionMax().x - buttonWidth -
		createButtonWidth - previewSettingsButtonWidth - 24.0f;
	const float nextX = (std::max)(ImGui::GetCursorPosX() + 16.0f, rightX);

	ImGui::SameLine(nextX);
	if (ImGui::Button(previewSettingsLabel, ImVec2(previewSettingsButtonWidth, 0.0f))) {

		showModelPreviewSettingsWindow_ = true;
		SavePersistentState();
	}

	ImGui::SameLine();
	if (ImGui::Button(createLabel, ImVec2(createButtonWidth, 0.0f))) {
		ImGui::OpenPopup("##ProjectCreateMenu");
	}
	if (ImGui::BeginPopup("##ProjectCreateMenu")) {

		DrawCreateMenuItems(selectedDirectory_);
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (ImGui::Button(label, ImVec2(buttonWidth, 0.0f))) {
		Rebuild(database);
		selectedAsset_ = {};
	}
}

void Engine::ProjectPanel::DrawSourceSelector([[maybe_unused]] const EditorPanelContext& context, AssetDatabase& database) {

	auto drawSourceButton = [&](ProjectAssetSource source, const char* label) {

		const bool selected = assetSource_ == source;
		if (selected) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button(label)) {

			assetSource_ = source;
			selectedDirectory_ = source == ProjectAssetSource::Engine ? "Engine/Assets" : "GameAssets";
			selectedAsset_ = {};
			Rebuild(database);
		}
		if (selected) {
			ImGui::EndDisabled();
		}
		};

	drawSourceButton(ProjectAssetSource::Engine, "Engine");
	ImGui::SameLine();
	drawSourceButton(ProjectAssetSource::Game, "Game");
	ImGui::Separator();
}

void Engine::ProjectPanel::DrawDirectoryContents(const EditorPanelContext& context,
	AssetDatabase& database, const ProjectDirectoryNode& node) {

	float iconSize = 64.0f;
	PrepareModelPreviewAtlas(context, database, node);

	int32_t columnCount = CalcGridColumnCount(ImGui::GetContentRegionAvail().x, iconSize + 8.0f);

	if (!ImGui::BeginTable("##ProjectGrid", columnCount, ImGuiTableFlags_SizingFixedFit)) {
		DrawDirectoryContextMenu(database, node);
		return;
	}

	// フォルダ
	for (const auto& child : node.children) {

		ImGui::TableNextColumn();
		ImGui::PushID(child->virtualPath.c_str());

		ImGui::BeginGroup();

		if (ImGui::ImageButton("##FolderButton", thumbnailCache_.GetFolderIconTextureID(), ImVec2(iconSize, iconSize),
			ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.16f, 0.16f, 0.16f, 1.0f))) {

			// Project内のフォルダ移動ではInspectorの選択状態を変更しない
			selectedDirectory_ = child->virtualPath;
			selectedAsset_ = {};
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			// ダブルクリックでもInspectorの選択状態は維持する
			selectedDirectory_ = child->virtualPath;
			selectedAsset_ = {};
		}
		DrawProjectFileMoveSource(child->virtualPath, true, child->name.c_str());

		ImGui::SetWindowFontScale(0.8f);
		ImGui::TextWrapped("%s", child->name.c_str());
		ImGui::SetWindowFontScale(1.0f);

		if (ImGui::BeginItemTooltip()) {
			ImGui::TextUnformatted(child->virtualPath.c_str());
			ImGui::EndTooltip();
		}

		ImGui::EndGroup();
		DrawProjectItemMoveDropTarget(database, child->virtualPath);
		DrawPrefabCreateDropTarget(context, database, child->virtualPath);
		DrawFolderContextMenu(database, *child);
		ImGui::PopID();
	}

	// アセット
	for (const auto& asset : node.assets) {

		ImGui::TableNextColumn();
		ImGui::PushID(asset.assetPath.c_str());

		ImGui::BeginGroup();

		ImTextureID textureID = thumbnailCache_.GetAssetTextureID(asset.assetPath, asset.type);
		ImVec2 uv0(0.0f, 0.0f);
		ImVec2 uv1(1.0f, 1.0f);
		if (asset.type == AssetType::Mesh) {

			ImTextureID previewTextureID = static_cast<ImTextureID>(0);
			ImVec2 previewUV0{};
			ImVec2 previewUV1{};
			if (TryGetModelPreviewImage(asset.assetID, previewTextureID, previewUV0, previewUV1)) {
				textureID = previewTextureID;
				uv0 = previewUV0;
				uv1 = previewUV1;
			}
		}

		if (ImGui::ImageButton("##AssetButton", textureID, ImVec2(iconSize, iconSize),
			uv0, uv1, ImVec4(0.16f, 0.16f, 0.16f, 1.0f))) {

			// 単クリックはProject内の選択だけに留め、Inspectorへは反映しない
			selectedAsset_ = asset.assetID;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			// Inspectorへ表示するのはダブルクリック時だけにする
			selectedAsset_ = asset.assetID;
			context.editorState->SelectAsset(asset.assetID);
			HandleAssetDoubleClick(context, asset);
		}

		DrawAssetDragDropSource(asset);

		ImGui::SetWindowFontScale(0.5f);
		ImGui::TextWrapped("%s", asset.displayName.c_str());
		ImGui::SetWindowFontScale(1.0f);
		DrawAssetDragDropSource(asset, ImGuiDragDropFlags_SourceAllowNullID);

		if (ImGui::BeginItemTooltip()) {
			ImGui::Text("Path: %s", asset.assetPath.c_str());
			ImGui::Text("Type: %s", EnumAdapter<AssetType>::ToString(asset.type));
			ImGui::Text("ID:   %s", Engine::ToString(asset.assetID).c_str());
			ImGui::EndTooltip();
		}

		ImGui::EndGroup();
		DrawAssetContextMenu(context, database, asset);
		ImGui::PopID();
	}

	ImGui::EndTable();

	ImGui::Spacing();
	ImGui::Button("Drop Here", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
	DrawPrefabCreateDropTarget(context, database, node.virtualPath);
	DrawProjectItemMoveDropTarget(database, node.virtualPath);
	DrawDirectoryContextMenu(database, node);
}

void Engine::ProjectPanel::DrawDirectoryContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node) {

	if (!ImGui::BeginPopupContextWindow("ProjectDirectoryContextMenu",
		ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		return;
	}

	if (ImGui::BeginMenu("Create")) {

		DrawCreateMenuItems(node.virtualPath);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Refresh")) {

		Rebuild(database);
		selectedAsset_ = {};
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawFolderContextMenu(AssetDatabase& database, const ProjectDirectoryNode& node) {

	if (!ImGui::BeginPopupContextItem("ProjectFolderContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	if (ImGui::MenuItem("Open")) {

		selectedDirectory_ = node.virtualPath;
		selectedAsset_ = {};
	}
	if (ImGui::BeginMenu("Create")) {

		DrawCreateMenuItems(node.virtualPath);
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Duplicate")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateDirectory(assetSource_, node.virtualPath);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("Delete")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DeleteDirectory(assetSource_, node.virtualPath);
		RefreshAfterFileOperation(database, result);
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawAssetContextMenu(const EditorPanelContext& context,
	AssetDatabase& database, const ProjectAssetEntry& asset) {

	if (!ImGui::BeginPopupContextItem("ProjectAssetContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
		return;
	}

	selectedAsset_ = asset.assetID;

	if (ImGui::MenuItem("Rename")) {

		BeginRenameAsset(asset);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Open")) {

		context.editorState->SelectAsset(asset.assetID);
		HandleAssetDoubleClick(context, asset);
	}
	if (ImGui::MenuItem("Duplicate")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DuplicateAsset(asset);
		RefreshAfterFileOperation(database, result);
	}
	if (ImGui::MenuItem("Delete")) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::DeleteAsset(asset);
		RefreshAfterFileOperation(database, result);
	}
	if (asset.type == AssetType::Prefab) {

		ImGui::Separator();
		if (ImGui::MenuItem("Instantiate Prefab", nullptr, false, context.CanEditScene())) {

			context.host->ExecuteEditorCommand(std::make_unique<InstantiatePrefabCommand>(asset.assetID));
		}
	}
	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawCreateAssetPopup(AssetDatabase& database) {

	if (requestOpenCreatePopup_) {

		ImGui::OpenPopup("Create Project Asset");
		requestOpenCreatePopup_ = false;
	}

	if (!ImGui::BeginPopupModal("Create Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	const char* kindLabel = ProjectAssetFileUtility::GetCreateMenuLabel(pendingCreateKind_);
	ImGui::Text("Create %s", kindLabel);
	ImGui::TextDisabled("%s", pendingCreateDirectory_.c_str());
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent(
		"Name",
		createNameBuffer_,
		createErrorMessage_.empty() ? nullptr : createErrorMessage_.c_str());

	if (inputResult.submitted) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::Create(
			assetSource_,
			pendingCreateDirectory_,
			pendingCreateKind_,
			createNameBuffer_);

		if (result.success) {

			createErrorMessage_.clear();
			RefreshAfterFileOperation(database, result);
			ImGui::CloseCurrentPopup();
		} else {

			createErrorMessage_ = result.message.empty() ? "Failed to create asset." : result.message;
		}
	}
	if (inputResult.canceled) {

		createErrorMessage_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::ProjectPanel::DrawRenameAssetPopup(AssetDatabase& database) {

	if (requestOpenRenamePopup_) {

		ImGui::OpenPopup("Rename Project Asset");
		requestOpenRenamePopup_ = false;
	}

	if (!ImGui::BeginPopupModal("Rename Project Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::Text("Rename Asset");
	ImGui::TextDisabled("%s", pendingRenameAsset_.assetPath.c_str());
	if (!renameProtectedSuffix_.empty()) {
		ImGui::TextDisabled("Protected suffix: %s", renameProtectedSuffix_.c_str());
	}
	ImGui::Separator();

	TextInputPopupResult inputResult = MyGUI::InputTextPopupContent(
		"Name",
		renameNameBuffer_,
		renameErrorMessage_.empty() ? nullptr : renameErrorMessage_.c_str());

	if (inputResult.submitted) {

		ProjectAssetFileResult result = ProjectAssetFileUtility::RenameAsset(
			pendingRenameAsset_,
			renameNameBuffer_);

		if (result.success) {

			renameErrorMessage_.clear();
			RefreshAfterFileOperation(database, result);
			ImGui::CloseCurrentPopup();
		} else {

			renameErrorMessage_ = result.message.empty() ? "Failed to rename asset." : result.message;
		}
	}
	if (inputResult.canceled) {

		renameErrorMessage_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void Engine::ProjectPanel::HandleAssetDoubleClick(const EditorPanelContext& context, const ProjectAssetEntry& asset) {

	if (asset.type == AssetType::Scene) {
		if (context.host) {
			context.host->RequestOpenScene(asset.assetID);
		}
		return;
	}
	if (asset.type != AssetType::Script) {
		return;
	}

	OpenScriptAssetInVisualStudio(asset);
}

bool Engine::ProjectPanel::SaveDroppedEntityAsPrefab(const EditorPanelContext& context,
	AssetDatabase& database, const std::string& directoryVirtualPath, const void* payloadData, int32_t payloadSize) {

	// HierarchyのドラッグペイロードはEntityの安定UUIDを保持している
	if (!context.CanEditScene() || !context.GetWorld() || payloadSize != sizeof(UUID) || payloadData == nullptr) {
		return false;
	}

	ECSWorld& world = *context.GetWorld();
	const UUID stableUUID = *static_cast<const UUID*>(payloadData);
	const Entity entity = world.FindByUUID(stableUUID);
	if (!world.IsAlive(entity)) {
		return false;
	}

	// Prefab名はEntity名を優先し、名前がなければNewPrefabにする
	std::string prefabName = "NewPrefab";
	if (world.HasComponent<NameComponent>(entity)) {

		const std::string& entityName = world.GetComponent<NameComponent>(entity).name;
		if (!entityName.empty()) {
			prefabName = entityName;
		}
	}

	ProjectAssetFileResult result = ProjectAssetFileUtility::Create(
		assetSource_,
		directoryVirtualPath,
		ProjectAssetFileKind::Prefab,
		prefabName);
	if (!result.success) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: failed to create prefab asset. message={}", result.message);
		return false;
	}

	PrefabSystem prefabSystem{};
	if (!prefabSystem.SavePrefab(database, world, entity, result.assetPath)) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: failed to save prefab. path={}", result.assetPath);
		return false;
	}

	const AssetID prefabAsset = database.ImportOrGet(result.assetPath, AssetType::Prefab);
	AttachPrefabLinkToSourceTree(world, entity, prefabAsset);

	RefreshAfterFileOperation(database, result);
	return true;
}

void Engine::ProjectPanel::DrawPrefabCreateDropTarget(const EditorPanelContext& context,
	AssetDatabase& database, const std::string& directoryVirtualPath) {

	if (!context.CanEditScene()) {
		return;
	}

	// Projectのフォルダまたは空白へHierarchy EntityをドロップするとPrefabを作成する
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayloadType)) {
		if (payload->IsDelivery()) {

			SaveDroppedEntityAsPrefab(context, database, directoryVirtualPath, payload->Data, payload->DataSize);
		}
	}
	ImGui::EndDragDropTarget();
}

bool Engine::ProjectPanel::MoveDroppedProjectItem(AssetDatabase& database,
	const std::string& targetDirectoryVirtualPath, const void* payloadData, int32_t payloadSize) {

	if (payloadData == nullptr || payloadSize != sizeof(EditorAssetDragDropPayload)) {
		return false;
	}

	const auto& payload = *static_cast<const EditorAssetDragDropPayload*>(payloadData);
	const std::string sourcePath = payload.assetPath;
	if (sourcePath.empty() || sourcePath == targetDirectoryVirtualPath) {
		return false;
	}

	ProjectAssetFileResult result{};
	if (payload.isDirectory != 0) {

		result = ProjectAssetFileUtility::MoveDirectory(assetSource_, sourcePath, targetDirectoryVirtualPath);
	} else {

		const ProjectAssetEntry* asset = assetIndex_.FindAssetByPath(sourcePath);
		if (!asset) {
			return false;
		}
		result = ProjectAssetFileUtility::MoveAsset(*asset, assetSource_, targetDirectoryVirtualPath);
	}

	RefreshAfterFileOperation(database, result);
	return result.success;
}

void Engine::ProjectPanel::DrawProjectItemMoveDropTarget(AssetDatabase& database,
	const std::string& targetDirectoryVirtualPath) {

	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
		if (payload->IsDelivery()) {

			MoveDroppedProjectItem(database, targetDirectoryVirtualPath, payload->Data, payload->DataSize);
		}
	}
	ImGui::EndDragDropTarget();
}

void Engine::ProjectPanel::BeginCreateAsset(ProjectAssetFileKind kind, const std::string& directoryVirtualPath) {

	pendingCreateKind_ = kind;
	pendingCreateDirectory_ = directoryVirtualPath;
	createNameBuffer_ = ProjectAssetFileUtility::GetDefaultName(kind);
	createErrorMessage_.clear();
	requestOpenCreatePopup_ = true;
}

void Engine::ProjectPanel::BeginRenameAsset(const ProjectAssetEntry& asset) {

	pendingRenameAsset_ = asset;
	renameNameBuffer_ = ProjectAssetFileUtility::GetEditableAssetName(asset);
	renameProtectedSuffix_ = ProjectAssetFileUtility::GetProtectedAssetSuffix(asset);
	renameErrorMessage_.clear();
	requestOpenRenamePopup_ = true;
}

void Engine::ProjectPanel::DrawCreateMenuItems(const std::string& directoryVirtualPath) {

	constexpr std::array<ProjectAssetFileKind, 9> kCreateKinds = {
		ProjectAssetFileKind::Folder,
		ProjectAssetFileKind::Script,
		ProjectAssetFileKind::Scene,
		ProjectAssetFileKind::Prefab,
		ProjectAssetFileKind::Material,
		ProjectAssetFileKind::AnimationClip,
		ProjectAssetFileKind::Shader,
		ProjectAssetFileKind::RenderPipeline,
		ProjectAssetFileKind::Text,
	};

	for (ProjectAssetFileKind kind : kCreateKinds) {

		if (ImGui::MenuItem(ProjectAssetFileUtility::GetCreateMenuLabel(kind))) {
			BeginCreateAsset(kind, directoryVirtualPath);
		}
	}
}

void Engine::ProjectPanel::RefreshAfterFileOperation(AssetDatabase& database, const ProjectAssetFileResult& result) {

	(void)database;

	if (!result.success) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: file operation failed. message={}", result.message);
		return;
	}

	// ProjectAssetIndexの参照を使っている描画中にRebuildすると、走査中のasset/nodeが破棄される。
	pendingFileOperationResult_ = result;
	hasPendingFileOperationRefresh_ = true;
	dirty_ = true;
}

void Engine::ProjectPanel::ApplyPendingFileOperationRefresh(AssetDatabase& database) {

	if (!hasPendingFileOperationRefresh_) {
		return;
	}

	const ProjectAssetFileResult result = pendingFileOperationResult_;
	pendingFileOperationResult_ = {};
	hasPendingFileOperationRefresh_ = false;

	Rebuild(database);

	if (result.isDirectory) {

		selectedDirectory_ = result.assetPath.empty() ? selectedDirectory_ : result.assetPath;
		selectedAsset_ = {};
		return;
	}

	if (const AssetMeta* meta = database.FindByPath(result.assetPath)) {
		selectedAsset_ = meta->guid;
	} else {
		selectedAsset_ = {};
	}
}

const char* Engine::ProjectPanel::GetSourceRootPath() const {

	switch (assetSource_) {
	case ProjectAssetSource::Engine:
		return "Engine/Assets";
	case ProjectAssetSource::Game:
		return "GameAssets";
	}
	return "Engine/Assets";
}

void Engine::ProjectPanel::LoadPersistentState() {

	const std::filesystem::path path = GetProjectPanelStatePath();
	if (!JsonAdapter::Check(path.string())) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path.string());
	if (!data.is_object()) {
		return;
	}

	assetSource_ = EnumAdapter<ProjectAssetSource>::FromString(data.value("assetSource", "Engine")).value();

	const std::string defaultDirectory = assetSource_ == ProjectAssetSource::Game ? "GameAssets" : "Engine/Assets";
	selectedDirectory_ = data.value("selectedDirectory", defaultDirectory);
	if (selectedDirectory_.empty()) {

		selectedDirectory_ = defaultDirectory;
	}
	selectedAsset_ = {};

	if (data.contains("modelPreview") && data["modelPreview"].is_object()) {

		const nlohmann::json& modelPreview = data["modelPreview"];
		showModelPreviewSettingsWindow_ = modelPreview.value("showSettingsWindow", showModelPreviewSettingsWindow_);
		if (modelPreview.contains("settings") && modelPreview["settings"].is_object()) {

			const nlohmann::json& settings = modelPreview["settings"];
			modelPreviewSettings_.tileSize = settings.value("tileSize", modelPreviewSettings_.tileSize);
			if (settings.contains("clearColor")) {

				modelPreviewSettings_.clearColor = Color4::FromJson(settings["clearColor"]);
			}
			modelPreviewSettings_.cameraFovY = settings.value("cameraFovY", modelPreviewSettings_.cameraFovY);
			modelPreviewSettings_.cameraDistanceScale =
				settings.value("cameraDistanceScale", modelPreviewSettings_.cameraDistanceScale);
			modelPreviewSettings_.cameraPitchDegrees =
				settings.value("cameraPitchDegrees", modelPreviewSettings_.cameraPitchDegrees);
			modelPreviewSettings_.cameraYawDegrees =
				settings.value("cameraYawDegrees", modelPreviewSettings_.cameraYawDegrees);
			if (settings.contains("lightDirection")) {

				modelPreviewSettings_.lightDirection = Vector3::FromJson(settings["lightDirection"]);
			}
			modelPreviewSettings_.lightIntensity = settings.value("lightIntensity", modelPreviewSettings_.lightIntensity);
		}
	}
	ClampModelPreviewSettings();
}

void Engine::ProjectPanel::SavePersistentState() const {

	nlohmann::json data = nlohmann::json::object();
	data["assetSource"] = EnumAdapter<ProjectAssetSource>::ToString(assetSource_);
	data["selectedDirectory"] = selectedDirectory_;
	data["modelPreview"] = {
		{"showSettingsWindow", showModelPreviewSettingsWindow_},
		{"settings", {
			{"tileSize", modelPreviewSettings_.tileSize},
			{"clearColor", modelPreviewSettings_.clearColor.ToJson()},
			{"cameraFovY", modelPreviewSettings_.cameraFovY},
			{"cameraDistanceScale", modelPreviewSettings_.cameraDistanceScale},
			{"cameraPitchDegrees", modelPreviewSettings_.cameraPitchDegrees},
			{"cameraYawDegrees", modelPreviewSettings_.cameraYawDegrees},
			{"lightDirection", modelPreviewSettings_.lightDirection.ToJson()},
			{"lightIntensity", modelPreviewSettings_.lightIntensity},
		}},
	};

	JsonAdapter::Save(GetProjectPanelStatePath().string(), data);
}
