#include "PostProcessStackTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessInputSources.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackSerializer.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>

// imgui
#include <imgui.h>
#include <imgui_stdlib.h>

// c++
#include <algorithm>
#include <filesystem>
#include <string>
#include <type_traits>

//============================================================================
//	PostProcessStackTool classMethods
//============================================================================
namespace {

	constexpr const char* kPassReorderPayloadType = "PP_PASS_REORDER";
	constexpr const char* kUnsavedPopupID = "UnsavedPostProcessStack##Popup";

	bool EndsWith(const std::string_view& str, const std::string_view& suffix) {

		if (str.size() < suffix.size()) {
			return false;
		}
		return str.rfind(suffix) == str.size() - suffix.size();
	}


	bool IsMaterialJsonFile(const std::string& path) {

		return EndsWith(path, ".material.json");
	}

	bool IsShaderJsonFile(const std::string& path) {

		return EndsWith(path, ".shader.json");
	}

	bool IsCsHlslFile(const std::string& path) {

		return EndsWith(path, ".CS.hlsl");
	}

	std::string MakeSamplerLabel(const Engine::ShaderResourceBinding& sampler) {

		std::string label = sampler.name.empty() ? std::string("Sampler") : sampler.name;
		label += " : s" + std::to_string(sampler.bindPoint);
		if (sampler.space != 0) {
			label += " space" + std::to_string(sampler.space);
		}
		return label;
	}

	bool DrawSamplerSettings(Engine::PipelineStaticSamplerSettings& settings) {

		bool changed = false;

		changed |= Engine::MyGUI::EnumCombo("フィルタ", settings.filter).valueChanged;
		changed |= Engine::MyGUI::EnumCombo("Address U", settings.addressU).valueChanged;
		changed |= Engine::MyGUI::EnumCombo("Address V", settings.addressV).valueChanged;
		changed |= Engine::MyGUI::EnumCombo("Address W", settings.addressW).valueChanged;
		changed |= Engine::MyGUI::EnumCombo("境界色", settings.borderColor).valueChanged;
		changed |= Engine::MyGUI::EnumCombo("比較", settings.comparisonFunc).valueChanged;

		return changed;
	}

	Engine::SceneHeader* ResolveActiveSceneHeader(const Engine::ToolContext& context) {

		// 通常はSceneInstanceManagerから実体のSceneHeaderを取得する
		if (context.sceneInstances && context.activeSceneInstanceID) {
			Engine::SceneInstance* activeScene = context.sceneInstances->Find(context.activeSceneInstanceID);
			if (activeScene) {
				return &activeScene->header;
			}
		}

		// 実体を指すactiveSceneHeaderを最後の手段として使う
		return const_cast<Engine::SceneHeader*>(context.activeSceneHeader);
	}

	// postProcessStack未設定のシーンで、シーンのベース直下PostProcess/に新規設定ファイルを作って結びつける
	// 既に設定があるシーンや解決できない場合は何もせず空を返す
	Engine::AssetID EnsureActiveStackAsset(const Engine::EditorToolContext& context) {

		const Engine::ToolContext& toolContext = context.toolContext;
		Engine::AssetDatabase* assetDatabase = toolContext.assetDatabase;
		Engine::SceneHeader* header = ResolveActiveSceneHeader(toolContext);
		if (!assetDatabase || !header || header->postProcessStack) {
			return {};
		}

		// 現在のシーンのアセットパスを解決する
		std::string scenePath;
		if (toolContext.sceneInstances && toolContext.activeSceneInstanceID) {
			if (const Engine::SceneInstance* instance = toolContext.sceneInstances->Find(toolContext.activeSceneInstanceID)) {
				if (const Engine::AssetMeta* meta = assetDatabase->Find(instance->sceneAsset)) {
					scenePath = meta->assetPath;
				}
			}
		}
		if (scenePath.empty()) {
			return {};
		}

		// シーンのベース込みの既定パスにフォルダを用意し、現在の設定でファイルを作る
		const std::string defaultPath = Engine::MakeDefaultPostProcessStackPath(scenePath);
		const std::filesystem::path fullPath = assetDatabase->ResolveAssetPath(defaultPath);
		std::error_code ec;
		std::filesystem::create_directories(fullPath.parent_path(), ec);

		Engine::PostProcessStackService& service = Engine::PostProcessStackService::GetInstance();
		Engine::PostProcessStackSerializer::Save(fullPath, service.GetSettings());

		// アセットとして登録し、シーンヘッダーへ結びつけてサービスのアクティブ設定にする
		const Engine::AssetID stackAsset = assetDatabase->ImportOrGet(defaultPath, Engine::AssetType::PostProcessStack);
		header->postProcessStack = stackAsset;
		service.SetActiveSettingsAsset(stackAsset, assetDatabase);
		return stackAsset;
	}
}

void Engine::PostProcessStackTool::Tick(ToolContext& context) {

	if (!context.activeSceneHeader) {
		return;
	}

	const AssetID nextStackAsset = context.activeSceneHeader->postProcessStack;
	if (nextStackAsset == lastStackAsset_) {
		return;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	if (service.IsDirty()) {

		// 変更がある場合は確認ポップアップを予約する
		pendingScenePathChange_ = true;
		pendingNextStackAsset_ = nextStackAsset;
	} else {

		service.SetActiveSettingsAsset(nextStackAsset, context.assetDatabase);
		lastStackAsset_ = nextStackAsset;
		selectedPassIndex_ = -1;
	}
}

void Engine::PostProcessStackTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::PostProcessStackTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::PostProcessStackTool::DrawWindow(const EditorToolContext& context) {

	ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("ポストプロセス設定", &openWindow_)) {
		ImGui::End();
		return;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	service.EnsureLoaded();

	// 現在の設定ファイルパスと未保存マーカーを表示する
	{
		const std::string pathStr = service.GetCurrentPath().generic_string();
		if (service.IsDirty()) {
			ImGui::TextDisabled("設定ファイル: %s *", pathStr.c_str());
		} else {
			ImGui::TextDisabled("設定ファイル: %s", pathStr.c_str());
		}
	}

	// Save / Reloadボタン
	if (ImGui::Button("保存")) {
		// 設定ファイルが無いシーンでは、シーンのベース直下PostProcess/へ新規作成してから保存する
		if (const AssetID created = EnsureActiveStackAsset(context)) {
			lastStackAsset_ = created;
			selectedPassIndex_ = -1;
		}
		service.Save();
		service.ClearDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("再読み込み")) {
		service.Reload();
		selectedPassIndex_ = -1;
	}

	// 別シーンのPostProcessStack設定ファイルを参照して現在のシーンへ結びつける
	if (SceneHeader* header = ResolveActiveSceneHeader(context.toolContext)) {
		AssetID picked = header->postProcessStack;
		AssetEditSetting setting{};
		if (MyGUI::AssetReferenceField("読み込み", picked, context.toolContext.assetDatabase,
			{ AssetType::PostProcessStack }, setting).valueChanged) {

			header->postProcessStack = picked;
			service.SetActiveSettingsAsset(picked, context.toolContext.assetDatabase);
			lastStackAsset_ = picked;
			selectedPassIndex_ = -1;
		}
	}

	ImGui::Separator();

	// .postProcessStack.jsonドロップゾーン
	DrawDropZones(context);

	ImGui::Separator();

	// 左右カラム分割:左=パス一覧、右=詳細
	const float totalWidth = ImGui::GetContentRegionAvail().x;
	const float leftWidth = totalWidth * 0.35f;
	const float rightWidth = totalWidth - leftWidth - ImGui::GetStyle().ItemSpacing.x;

	ImGui::BeginGroup();
	ImGui::SetNextWindowContentSize(ImVec2(leftWidth, 0.0f));
	if (ImGui::BeginChild("##PassList", ImVec2(leftWidth, 0.0f), true)) {
		DrawPassList();
	}
	ImGui::EndChild();
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginGroup();
	if (ImGui::BeginChild("##PassDetail", ImVec2(rightWidth, 0.0f), true)) {
		DrawPassDetail(context);
	}
	ImGui::EndChild();
	ImGui::EndGroup();

	// 未保存確認ポップアップ
	DrawUnsavedConfirmPopup(context);

	ImGui::End();
}

void Engine::PostProcessStackTool::DrawPassList() {

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	PostProcessStackSettings& settings = service.GetSettings();
	auto& passes = settings.passes;

	ImGui::TextUnformatted("パス一覧");
	ImGui::Separator();

	int32_t reorderFrom = -1;
	int32_t reorderTo = -1;

	for (int32_t i = 0; i < static_cast<int32_t>(passes.size()); ++i) {

		auto& pass = passes[i];
		ImGui::PushID(i);

		// 有効チェックボックス
		if (ImGui::Checkbox("##en", &pass.enabled)) {
			service.MarkDirty();
			service.RebuildRuntime();
		}
		ImGui::SameLine();

		// パス名の選択アイテム
		const bool isSelected = (selectedPassIndex_ == i);
		if (ImGui::Selectable(pass.name.empty() ? "(名前なし)" : pass.name.c_str(),
			isSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f))) {
			selectedPassIndex_ = i;
		}

		// ドラッグソース:パスの並び替え
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			ImGui::SetDragDropPayload(kPassReorderPayloadType, &i, sizeof(int32_t));
			ImGui::TextUnformatted(pass.name.empty() ? "(名前なし)" : pass.name.c_str());
			ImGui::EndDragDropSource();
		}

		// ドラッグターゲット:ここへドロップで並び替える
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPassReorderPayloadType)) {
				reorderFrom = *static_cast<const int32_t*>(payload->Data);
				reorderTo = i;
			}
			ImGui::EndDragDropTarget();
		}

		// 右クリックコンテキストメニュー
		if (ImGui::BeginPopupContextItem("##PassContext")) {
			if (ImGui::MenuItem("削除")) {
				passes.erase(passes.begin() + i);
				if (selectedPassIndex_ >= static_cast<int32_t>(passes.size())) {
					selectedPassIndex_ = static_cast<int32_t>(passes.size()) - 1;
				}
				service.MarkDirty();
				service.RebuildRuntime();
				ImGui::EndPopup();
				ImGui::PopID();
				break;
			}
			ImGui::EndPopup();
		}

		ImGui::PopID();
	}

	// 並び替え処理
	if (reorderFrom >= 0 && reorderTo >= 0 && reorderFrom != reorderTo) {

		const int32_t passCount = static_cast<int32_t>(passes.size());
		if (reorderFrom < passCount && reorderTo < passCount) {

			PostProcessStackPassSettings moved = std::move(passes[reorderFrom]);
			passes.erase(passes.begin() + reorderFrom);
			const int32_t insertAt = (reorderTo > reorderFrom) ? reorderTo - 1 : reorderTo;
			passes.insert(passes.begin() + insertAt, std::move(moved));

			if (selectedPassIndex_ == reorderFrom) {
				selectedPassIndex_ = insertAt;
			}
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}
}

void Engine::PostProcessStackTool::DrawPassDetail(const EditorToolContext& context) {

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	PostProcessStackSettings& stackSettings = service.GetSettings();
	auto& passes = stackSettings.passes;

	if (selectedPassIndex_ < 0 || selectedPassIndex_ >= static_cast<int32_t>(passes.size())) {
		ImGui::TextDisabled("編集するパスを選択してください");
		return;
	}

	PostProcessStackPassSettings& pass = passes[selectedPassIndex_];

	// 選択中パスをプレビュー対象としてPostProcessStackPassへ伝える
	service.SetPreviewPassId(pass.id);

	// 選択中パスのシェーダーを再コンパイルしてパラメータを再読み込みする
	if (ImGui::Button("リフレクション再読み込み")) {
		PostProcessStackService::GetInstance().RequestShaderReload(pass.materialGuid);
	}

	ImGui::Separator();

	// パス名編集
	if (MyGUI::InputText("名前", pass.name).editFinished) {
		service.MarkDirty();
	}

	// このパスを差し込む固定パス上の位置
	if (MyGUI::EnumCombo("実行パス位置", pass.anchor).editFinished) {
		service.MarkDirty();
		service.RebuildRuntime();
	}

	// 主入力は空なら一覧上の直前、明示時は選択したパス出力を使う
	const auto findPassName =
		[&](UUID id) -> const char* {

		for (const PostProcessStackPassSettings& candidate :
			passes) {

			if (candidate.id == id) {
				return candidate.name.c_str();
			}
		}
		return "(参照なし)";
		};
	const char* sourcePreview =
		pass.sourcePass ?
		findPassName(pass.sourcePass) :
		"直前のパス";
	if (ImGui::BeginCombo(
		"主入力", sourcePreview)) {

		if (ImGui::Selectable(
			"直前のパス", !pass.sourcePass)) {

			pass.sourcePass = {};
			service.MarkDirty();
			service.RebuildRuntime();
		}
		for (const PostProcessStackPassSettings& candidate :
			passes) {

			if (candidate.id == pass.id ||
				candidate.anchor != pass.anchor) {
				continue;
			}
			if (ImGui::Selectable(
				candidate.name.c_str(),
				pass.sourcePass == candidate.id)) {

				pass.sourcePass = candidate.id;
				service.MarkDirty();
				service.RebuildRuntime();
			}
		}
		ImGui::EndCombo();
	}

	bool graphOutput = pass.graphOutput;
	if (MyGUI::Checkbox(
		"最終出力", graphOutput)) {

		pass.graphOutput = graphOutput;
		if (graphOutput) {
			for (PostProcessStackPassSettings& candidate :
				passes) {

				if (candidate.id != pass.id &&
					candidate.anchor == pass.anchor) {
					candidate.graphOutput = false;
				}
			}
		}
		service.MarkDirty();
		service.RebuildRuntime();
	}

	int32_t targetMask =
		static_cast<int32_t>(pass.targetMask);
	if (MyGUI::DragInt(
		"対象マスク", targetMask,
		{ .minValue = 0,
		  .maxValue = static_cast<int32_t>(
			  kRenderingLayerMaskBits) }).valueChanged) {

		pass.targetMask =
			static_cast<uint32_t>(targetMask);
		service.MarkDirty();
		service.RebuildRuntime();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip(
			"0は全画面、1以上はMesh、Primitive、FillMeshの不透明描画で"
			"\n描画対象マスクと一致する画素だけへ適用します");
	}

	ImGui::Separator();

	// マテリアル参照フィールド
	{
		AssetID matGuid = pass.materialGuid;
		AssetEditSetting setting{};
		if (MyGUI::AssetReferenceField("マテリアル", matGuid,
			context.toolContext.assetDatabase, { AssetType::Material }, setting).valueChanged) {

			pass.materialGuid = matGuid;
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}

	ImGui::Separator();

	// CBufferパラメータオーバーライドUI (Reflectionキャッシュ使用)
	const std::vector<ShaderConstantBufferVariable>* vars = service.FindReflectionVars(pass.materialGuid);
	if (vars && !vars->empty()) {

		ImGui::TextUnformatted("パラメータ");
		ImGui::Separator();

		bool anyParamChanged = false;
		for (const auto& var : *vars) {

			// pad / paddingはHLSLのアライメント調整用変数のため表示しない
			if (var.name.find("pad") != std::string::npos || var.name.find("Pad") != std::string::npos) {
				continue;
			}

			ImGui::PushID(var.name.c_str());

			auto it = pass.parameterOverrides.find(var.name);
			if (it != pass.parameterOverrides.end()) {

				// オーバーライドあり:そのまま編集、xボタンでデフォルトへ戻す
				if (MaterialParameterEditor::DrawValueEdit(var, it->second).valueChanged) {
					anyParamChanged = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x##RemoveOverride")) {
					pass.parameterOverrides.erase(it);
					anyParamChanged = true;
				}
			} else {

				// オーバーライドが無くても最初から編集可能にする、編集した時点でオーバーライドを作る
				MaterialParameterValue temp = MaterialParameterEditor::DefaultValueForVariable(var);
				if (MaterialParameterEditor::DrawValueEdit(var, temp).valueChanged) {
					pass.parameterOverrides[var.name] = temp;
					anyParamChanged = true;
				}
			}

			ImGui::PopID();
		}

		if (anyParamChanged) {
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}

	// テクスチャSRVオーバーライドUI (Reflectionキャッシュ使用)
	const std::vector<ShaderResourceBinding>* srvs = service.FindReflectionSRVs(pass.materialGuid);
	if (srvs && !srvs->empty()) {

		ImGui::TextUnformatted("テクスチャ");
		ImGui::Separator();

		bool anySRVChanged = false;
		for (const auto& srv : *srvs) {

			ImGui::PushID(srv.name.c_str());

			// 任意の先行パス出力をSRVへ接続する
			auto passInput =
				pass.passInputs.find(srv.name);
			const UUID currentPassInput =
				passInput != pass.passInputs.end() ?
				passInput->second : UUID{};
			const std::string passLabel =
				srv.name + " (パス出力)";
			if (ImGui::BeginCombo(
				passLabel.c_str(),
				currentPassInput ?
				findPassName(currentPassInput) :
				"(なし)")) {

				if (ImGui::Selectable(
					"(なし)", !currentPassInput)) {

					pass.passInputs.erase(srv.name);
					anySRVChanged = true;
				}
				for (const PostProcessStackPassSettings&
					candidate : passes) {

					if (candidate.id == pass.id ||
						candidate.anchor != pass.anchor) {
						continue;
					}
					if (ImGui::Selectable(
						candidate.name.c_str(),
						currentPassInput ==
							candidate.id)) {

						pass.passInputs[srv.name] =
							candidate.id;
						pass.renderTargetInputs.erase(
							srv.name);
						anySRVChanged = true;
					}
				}
				ImGui::EndCombo();
			}

			// 中間RT(GBuffer/深度など)の割り当て、設定すると.pngより優先される
			auto rtIt = pass.renderTargetInputs.find(srv.name);
			const std::string currentRT = (rtIt != pass.renderTargetInputs.end()) ? rtIt->second : std::string();
			const std::string rtLabel = srv.name + " (RT)";
			if (ImGui::BeginCombo(rtLabel.c_str(), currentRT.empty() ? "(なし)" : currentRT.c_str())) {

				if (ImGui::Selectable("(なし)", currentRT.empty())) {
					pass.renderTargetInputs.erase(srv.name);
					anySRVChanged = true;
				}
				for (const char* sourceName : kPostProcessInputSources) {
					if (ImGui::Selectable(sourceName, currentRT == sourceName)) {
						pass.renderTargetInputs[srv.name] = sourceName;
						pass.passInputs.erase(srv.name);
						anySRVChanged = true;
					}
				}
				ImGui::EndCombo();
			}

			// .png等の個別テクスチャ割り当て、RT未指定時のフォールバック入力
			auto it = pass.textureGuids.find(srv.name);
			AssetID texGuid = (it != pass.textureGuids.end()) ? it->second : AssetID{};

			AssetEditSetting setting{};
			if (MyGUI::AssetReferenceField(srv.name.c_str(), texGuid,
				context.toolContext.assetDatabase, { AssetType::Texture }, setting).valueChanged) {

				if (texGuid) {
					pass.textureGuids[srv.name] = texGuid;
				} else {
					pass.textureGuids.erase(srv.name);
				}
				anySRVChanged = true;
			}

			ImGui::PopID();
		}

		if (anySRVChanged) {
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}

	// SamplerStateオーバーライドUI
	const std::vector<ShaderResourceBinding>* samplers = service.FindReflectionSamplers(pass.materialGuid);
	if (samplers && !samplers->empty()) {

		ImGui::TextUnformatted("サンプラー");
		ImGui::Separator();

		bool anySamplerChanged = false;
		for (const auto& sampler : *samplers) {

			if (sampler.name.empty()) {
				ImGui::PushID(static_cast<int32_t>(sampler.bindPoint));
			} else {
				ImGui::PushID(sampler.name.c_str());
			}

			const std::string label = MakeSamplerLabel(sampler);
			if (MyGUI::CollapsingHeader(label.c_str(), true)) {

				auto it = sampler.name.empty() ? pass.samplerOverrides.end() : pass.samplerOverrides.find(sampler.name);
				PipelineStaticSamplerSettings settings =
					(it != pass.samplerOverrides.end()) ? it->second : PipelineStaticSamplerSettings{};

				ImGui::BeginDisabled(sampler.name.empty());
				if (ImGui::SmallButton("デフォルトへ戻す")) {
					if (it != pass.samplerOverrides.end()) {
						pass.samplerOverrides.erase(it);
						anySamplerChanged = true;
					}
				}
				ImGui::EndDisabled();

				if (sampler.name.empty()) {
					ImGui::TextDisabled("SamplerStateに名前を付けてください");
				} else if (DrawSamplerSettings(settings)) {
					pass.samplerOverrides[sampler.name] = settings;
					anySamplerChanged = true;
				}
			}

			ImGui::PopID();
		}

		if (anySamplerChanged) {
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}

	ImGui::Separator();

	// 選択中パスの実行前後プレビュー(デフォルトは閉じておく)
	if (MyGUI::CollapsingHeader("プレビュー", false)) {

		const PostProcessStackService::PreviewImage& preview = service.GetPreviewImage();
		if (!preview.valid) {
			ImGui::TextDisabled("プレビューはありません");
		} else {

			// 表示できる範囲の幅から16:9でサイズを決める
			const float availWidth = ImGui::GetContentRegionAvail().x;
			const ImVec2 imageSize(availWidth, availWidth * 9.0f / 16.0f);

			ImGui::TextUnformatted("適用前");
			ImGui::Image(static_cast<ImTextureID>(preview.beforeSrvPtr), imageSize);

			ImGui::TextUnformatted("適用後");
			ImGui::Image(static_cast<ImTextureID>(preview.afterSrvPtr), imageSize);
		}
	}
}

void Engine::PostProcessStackTool::DrawDropZones(const EditorToolContext& context) {

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	PostProcessStackSettings& settings = service.GetSettings();

	// ドロップゾーン: .postProcessStack.jsonを読み込む
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		const float zoneHeight = 24.0f;
		ImGui::Button(".postProcessStack.json をドロップで読み込み", ImVec2(ImGui::GetContentRegionAvail().x, zoneHeight));
		ImGui::PopStyleColor();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				IEditorPanel::kProjectAssetDragDropPayloadType)) {

				const auto* data = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (data && !data->isDirectory && data->assetType == AssetType::PostProcessStack) {

					const std::string stackPath = data->assetPath;
					const AssetID stackAsset = data->assetID;
					SceneHeader* activeSceneHeader = ResolveActiveSceneHeader(context.toolContext);
					if (activeSceneHeader) {
						activeSceneHeader->postProcessStack = stackAsset;
					} else {
						Logger::Output(LogType::Engine,
							"[PostProcessStack] Dropped stack was loaded, but active SceneHeader was not found: " +
							stackPath);
					}

					service.SetActiveSettingsAsset(stackAsset, context.toolContext.assetDatabase);
					lastStackAsset_ = stackAsset;
					selectedPassIndex_ = -1;

					Logger::Output(LogType::Engine,
						"[PostProcessStack] Applied stack to active scene: " + stackPath);
				}
			}
			ImGui::EndDragDropTarget();
		}
	}

	// ドロップゾーン:マテリアル/シェーダー/ HLSLを追加する
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		const float zoneHeight = 24.0f;
		ImGui::Button(".material.json / .shader.json / .CS.hlsl をドロップでパス追加",
			ImVec2(ImGui::GetContentRegionAvail().x, zoneHeight));
		ImGui::PopStyleColor();

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				IEditorPanel::kProjectAssetDragDropPayloadType)) {

				const auto* data = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
				if (data && !data->isDirectory) {

					const std::string assetPath = data->assetPath;
					AssetID materialGuid{};
					std::string name = std::filesystem::path(assetPath).stem().string();

					if (IsMaterialJsonFile(assetPath)) {

						// .material.jsonドロップ: GUIDを直接取得する
						materialGuid = data->assetID;
						if (!materialGuid && context.toolContext.assetDatabase) {
							const AssetMeta* meta = context.toolContext.assetDatabase->FindByPath(assetPath);
							if (meta) {
								materialGuid = meta->guid;
							}
						}
						if (!materialGuid) {
							Logger::Output(LogType::Engine,
								"[PostProcessStack] Failed to resolve material GUID: " + assetPath);
						}

						// 拡張子を取り除いた名前を使う
						if (name.size() > 9 && name.substr(name.size() - 9) == ".material") {
							name = name.substr(0, name.size() - 9);
						}
					} else if (IsShaderJsonFile(assetPath)) {

						// .shader.jsonドロップ:対応するMaterialを検索または生成する
						materialGuid = PostProcessAssetGenerator::FindOrCreateMaterialForShader(
							context.toolContext.assetDatabase, assetPath);
						if (!materialGuid) {
							Logger::Output(LogType::Engine,
								"[PostProcessStack] Failed to find or create material for shader: " + assetPath);
						}

						// .shader.json の stem から baseName を取得する ("Bloom.shader" -> "Bloom")
						const std::string stem1 = name; // "Bloom.shader"
						name = std::filesystem::path(stem1).stem().string(); // "Bloom"
					} else if (IsCsHlslFile(assetPath)) {

						// .CS.hlslドロップ: shader/pipeline/materialを生成または検索する
						materialGuid = PostProcessAssetGenerator::EnsureUserAsset(
							context.toolContext.assetDatabase, assetPath);
						if (!materialGuid) {
							Logger::Output(LogType::Engine,
								"[PostProcessStack] Failed to generate assets for: " + assetPath);
						}

						// .CS.hlsl の stem から baseName を取得する ("Bloom.CS" -> "Bloom")
						const std::string stem1 = name; // "Bloom.CS"
						name = std::filesystem::path(stem1).stem().string(); // "Bloom"
					} else {
						Logger::Output(LogType::Engine,
							"[PostProcessStack] Unsupported file dropped: " + assetPath +
							". Drag .material.json, .shader.json, or .CS.hlsl.");
					}

					if (materialGuid) {

						// 既存のmaterialアセットパスキャッシュを解決する
						std::string materialPath = assetPath;
						if (!IsMaterialJsonFile(assetPath) && context.toolContext.assetDatabase) {
							const AssetMeta* meta = context.toolContext.assetDatabase->Find(materialGuid);
							if (meta) {
								materialPath = meta->assetPath;
							}
						}

						PostProcessStackPassSettings newPass{};
						newPass.id = UUID::New();
						newPass.name = name.empty() ? "NewPass" : name;
						newPass.enabled = true;
						newPass.materialGuid = materialGuid;
						newPass.passKind = MaterialPassKind::PostProcess;

						settings.passes.emplace_back(std::move(newPass));
						selectedPassIndex_ = static_cast<int32_t>(settings.passes.size()) - 1;
						service.MarkDirty();
						service.RebuildRuntime();

						Logger::Output(LogType::Engine,
							"[PostProcessStack] Added pass '" + newPass.name + "' material=" + materialPath);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
}

void Engine::PostProcessStackTool::DrawUnsavedConfirmPopup(const EditorToolContext& context) {

	if (pendingScenePathChange_) {
		ImGui::OpenPopup(kUnsavedPopupID);
		pendingScenePathChange_ = false;
	}

	ImGui::SetNextWindowSize(ImVec2(340.0f, 120.0f), ImGuiCond_Always);
	if (!ImGui::BeginPopupModal(kUnsavedPopupID, nullptr,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
		return;
	}

	ImGui::TextUnformatted("ポストプロセス設定に未保存の変更があります");
	ImGui::TextUnformatted("保存せずにシーンを切り替えますか？");
	ImGui::Spacing();

	PostProcessStackService& service = PostProcessStackService::GetInstance();

	if (ImGui::Button("保存して切り替え", ImVec2(110.0f, 0.0f))) {
		service.Save();
		service.ClearDirty();
		service.SetActiveSettingsAsset(pendingNextStackAsset_, context.toolContext.assetDatabase);
		lastStackAsset_ = pendingNextStackAsset_;
		selectedPassIndex_ = -1;
		pendingScenePathChange_ = false;
		pendingNextStackAsset_ = {};
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("破棄して切り替え", ImVec2(110.0f, 0.0f))) {
		service.SetActiveSettingsAsset(pendingNextStackAsset_, context.toolContext.assetDatabase);
		lastStackAsset_ = pendingNextStackAsset_;
		selectedPassIndex_ = -1;
		pendingScenePathChange_ = false;
		pendingNextStackAsset_ = {};
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(80.0f, 0.0f))) {
		pendingScenePathChange_ = false;
		pendingNextStackAsset_ = {};
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}
