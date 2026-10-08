#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include "ProjectPanelUI.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Scripting/ManagedIDELauncher.h>
#include <Engine/Editor/Utility/EditorShell.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphEditorTool.h>
#include <Engine/Editor/Tools/Builtin/Effect/ParticleEffectEditorTool.h>
#include <Engine/Editor/Tools/Builtin/RenderFeatures/RenderFeatureProfileTool.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <cstring>
#include <filesystem>
#include <system_error>

namespace Engine::ProjectPanelUI {

	// アセットアイコンの標準解決
	ImTextureID ResolveDefaultAssetIcon(
		Engine::ProjectAssetThumbnailCache& thumbnailCache, const Engine::ProjectAssetEntry& asset) {

		return thumbnailCache.GetAssetTextureID(asset.assetPath, asset.type);
	}

	// ドラッグ&ドロップの標準ソースを描画する
	void DrawDefaultAssetDragDropSource(const Engine::ProjectAssetEntry& asset, ImGuiDragDropFlags flags) {

		if (ImGui::BeginDragDropSource(flags)) {

			Engine::EditorAssetDragDropPayload payload{};
			payload.assetID = asset.assetID;
			payload.assetType = asset.type;
			payload.isDirectory = 0;
			strncpy_s(payload.assetPath, asset.assetPath.c_str(), sizeof(payload.assetPath) - 1);

			ImGui::SetDragDropPayload(Engine::IEditorPanel::kProjectAssetDragDropPayloadType, &payload, sizeof(payload));

			ImGui::TextUnformatted(asset.displayName.c_str());
			ImGui::TextDisabled("%s", asset.assetPath.c_str());

			ImGui::EndDragDropSource();
		}
	}
}

namespace {

	// ScriptアセットをVisual Studioで開く
	bool OpenScriptAssetInVisualStudio(const Engine::ProjectAssetEntry& asset) {

		const std::filesystem::path scriptPath = Engine::RuntimePaths::ResolveAssetPath(asset.assetPath);
		std::error_code scriptEc;
		if (scriptPath.empty() || !std::filesystem::exists(scriptPath, scriptEc) || scriptEc) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"ProjectPanel: Scriptファイルが見つかりません path={}", asset.assetPath);
			return false;
		}

		// 共通のIDE設定でScriptを開く

		return Engine::ManagedIDELauncher::OpenFile(scriptPath, 1, 1);
	}
}

using namespace Engine::ProjectPanelUI;

void Engine::ProjectPanel::RegisterAssetActions() {

	// Sceneはエディターへ開く要求を出す
	AssetActionDescriptor scene{};
	scene.type = AssetType::Scene;
	scene.displayName = "Scene";
	scene.iconResolver = ResolveDefaultAssetIcon;
	scene.onDoubleClick = [](const EditorPanelContext& context, const ProjectAssetEntry& asset) {
		if (context.host) {
			context.host->RequestOpenScene(asset.assetID);
		}
	};
	scene.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(scene));

	// Prefabを隔離Worldで開く
	AssetActionDescriptor prefab{};
	prefab.type = AssetType::Prefab;
	prefab.displayName = "Prefab";
	prefab.iconResolver = ResolveDefaultAssetIcon;
	prefab.onDoubleClick = [](const EditorPanelContext& context, const ProjectAssetEntry& asset) {
		if (context.host) {
			context.host->RequestEnterPrefabEdit(asset.assetID);
		}
	};
	prefab.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(prefab));

	// Scriptは共通IDE launcherで開く
	AssetActionDescriptor script{};
	script.type = AssetType::Script;
	script.displayName = "Script";
	script.iconResolver = ResolveDefaultAssetIcon;
	script.onDoubleClick = [](const EditorPanelContext& /*context*/, const ProjectAssetEntry& asset) {
		OpenScriptAssetInVisualStudio(asset);
	};
	script.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(script));

	// ShaderGraphは専用ノードエディターで対象アセットを開く
	AssetActionDescriptor shaderGraph{};
	shaderGraph.type = AssetType::ShaderGraph;
	shaderGraph.displayName = "ShaderGraph";
	shaderGraph.iconResolver = ResolveDefaultAssetIcon;
	shaderGraph.onDoubleClick = [](const EditorPanelContext& /*context*/, const ProjectAssetEntry& asset) {
		ITool* tool = ToolRegistry::GetInstance().Find("engine.shader_graph");
		auto* editor = dynamic_cast<ShaderGraphEditorTool*>(tool);
		if (editor) {
			editor->OpenAsset(asset.assetID);
		}
	};
	shaderGraph.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(shaderGraph));

	// ParticleEffectは専用エフェクト編集ツールで対象アセットを開く
	AssetActionDescriptor particleEffect{};
	particleEffect.type = AssetType::ParticleEffect;
	particleEffect.displayName = "ParticleEffect";
	particleEffect.iconResolver = ResolveDefaultAssetIcon;
	particleEffect.onDoubleClick = [](const EditorPanelContext& /*context*/, const ProjectAssetEntry& asset) {
		ITool* tool = ToolRegistry::GetInstance().Find("engine.particle_effect_editor");
		auto* editor = dynamic_cast<ParticleEffectEditorTool*>(tool);
		if (editor) {
			editor->OpenAsset(asset.assetID);
		}
	};
	particleEffect.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(particleEffect));

	// Render Passesは既存のPass Graph編集機能で開く
	AssetActionDescriptor renderPasses{};
	renderPasses.type = AssetType::RenderPasses;
	renderPasses.displayName = "RenderPasses";
	renderPasses.iconResolver = ResolveDefaultAssetIcon;
	renderPasses.onDoubleClick = [](const EditorPanelContext&, const ProjectAssetEntry& asset) {
		ITool* tool = ToolRegistry::GetInstance().Find("engine.render_features");
		auto* editor = dynamic_cast<RenderFeatureProfileTool*>(tool);
		if (editor) {
			editor->OpenAsset(asset.assetID);
		}
	};
	renderPasses.onDragSource = DrawDefaultAssetDragDropSource;
	assetActionRegistry_.Register(std::move(renderPasses));
}

void Engine::ProjectPanel::HandleAssetDoubleClick(const EditorPanelContext& context, const ProjectAssetEntry& asset) {

	// テキストはOSの関連付けで開く
	const std::string extension =
		Algorithm::ToLower(Algorithm::PathToUTF8(Algorithm::PathFromUTF8(asset.assetPath).extension()));
	if (extension == ".txt") {

		EditorShell::OpenWithSystemDefault(RuntimePaths::ResolveAssetPath(asset.assetPath));
		return;
	}

	// アセット種別ごとのダブルクリック処理はRegistryへ委ねる
	if (const AssetActionDescriptor* action = assetActionRegistry_.Find(asset.type)) {
		if (action->onDoubleClick) {
			action->onDoubleClick(context, asset);
		}
	}
}
