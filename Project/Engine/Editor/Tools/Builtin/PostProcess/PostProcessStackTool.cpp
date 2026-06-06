#include "PostProcessStackTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/IDentity/UUID.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

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
	constexpr const char* kUnsavedPopupId = "UnsavedPostProcessStack##Popup";

	bool EndsWith(const std::string_view& str, const std::string_view& suffix) {

		if (str.size() < suffix.size()) {
			return false;
		}
		return str.rfind(suffix) == str.size() - suffix.size();
	}

	uint32_t GetScalarComponentCount(const Engine::ShaderConstantBufferVariable& var) {

		uint32_t count = (std::max)(1u, var.declaredComponentCount);
		if (var.columns > 0) {
			count = (std::max)(count, var.columns);
		}
		if (var.rows > 0 && var.columns > 0) {
			count = (std::max)(count, var.rows * var.columns);
		}
		if (count <= 1 && var.size > sizeof(float)) {
			count = static_cast<uint32_t>(var.size / sizeof(float));
		}
		return (std::min)(count, 4u);
	}

	bool IsColorParameterName(const std::string& name) {

		return name.find("color") != std::string::npos ||
			name.find("Color") != std::string::npos ||
			name.find("tint") != std::string::npos ||
			name.find("Tint") != std::string::npos;
	}

	// 変数タイプからMaterialParameterValueを生成する
	Engine::MaterialParameterValue DefaultValueForVariable(const Engine::ShaderConstantBufferVariable& var) {

		Engine::MaterialParameterValue result{};
		const bool isColor = IsColorParameterName(var.name);
		if (var.valueType == D3D_SVT_FLOAT) {
			const uint32_t componentCount = GetScalarComponentCount(var);
			if (componentCount <= 1) {
				result.value = 0.0f;
			} else if (componentCount == 2) {
				result.value = Engine::Vector2{};
			} else if (componentCount == 3) {
				result.value = Engine::Vector3{};
			} else {
				if (isColor) {
					result.value = Engine::Color4(0.0f, 0.0f, 0.0f, 1.0f);
				} else {
					result.value = Engine::Vector4{};
				}
			}
		} else if (var.valueType == D3D_SVT_INT) {
			result.value = int32_t(0);
		} else if (var.valueType == D3D_SVT_UINT) {
			result.value = uint32_t(0);
		} else if (var.valueType == D3D_SVT_BOOL) {
			result.value = false;
		} else {
			result.value = 0.0f;
		}
		return result;
	}

	// バリアントからi番目のfloat成分を取り出す（型が違っても安全に変換する）
	float ExtractFloatComponent(const Engine::MaterialParameterValue& value, int idx) {

		return std::visit([idx](const auto& v) -> float {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, float>) {
				return (idx == 0) ? v : 0.0f;
			} else if constexpr (std::is_same_v<T, Engine::Vector2>) {
				return (idx == 0) ? v.x : (idx == 1 ? v.y : 0.0f);
			} else if constexpr (std::is_same_v<T, Engine::Vector3>) {
				return (idx == 0) ? v.x : (idx == 1 ? v.y : (idx == 2 ? v.z : 0.0f));
			} else if constexpr (std::is_same_v<T, Engine::Vector4>) {
				return (idx == 0) ? v.x : (idx == 1 ? v.y : (idx == 2 ? v.z : (idx == 3 ? v.w : 0.0f)));
			} else if constexpr (std::is_same_v<T, Engine::Color4>) {
				return (idx == 0) ? v.r : (idx == 1 ? v.g : (idx == 2 ? v.b : (idx == 3 ? v.a : 0.0f)));
			} else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) {
				return (idx == 0) ? static_cast<float>(v) : 0.0f;
			} else if constexpr (std::is_same_v<T, bool>) {
				return (idx == 0 && v) ? 1.0f : 0.0f;
			} else {
				return 0.0f;
			}
		}, value.value);
	}

	// varの型情報とラベル名に基づいてUIウィジェットを表示し値を更新する
	// バリアントの格納型ではなくリフレクションの成分数/var.valueTypeを基準にするため型不一致のバグが出ない
	bool DrawParameterValueEdit(const Engine::ShaderConstantBufferVariable& var, Engine::MaterialParameterValue& value) {

		const char* label = var.name.c_str();
		const bool isColor = IsColorParameterName(var.name);
		const uint32_t componentCount = GetScalarComponentCount(var);

		if (var.valueType == D3D_SVT_FLOAT) {

			if (componentCount <= 1) {
				float v = ExtractFloatComponent(value, 0);
				if (Engine::MyGUI::DragFloat(label, v).valueChanged) {
					value.value = v;
					return true;
				}
			} else if (componentCount == 2) {
				Engine::Vector2 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1) };
				if (Engine::MyGUI::DragVector2(label, v).valueChanged) {
					value.value = v;
					return true;
				}
			} else if (componentCount == 3) {
				if (isColor) {
					Engine::Color3 c{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2) };
					if (Engine::MyGUI::ColorEdit(label, c).valueChanged) {
						value.value = Engine::Vector3{ c.r, c.g, c.b };
						return true;
					}
				} else {
					Engine::Vector3 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2) };
					if (Engine::MyGUI::DragVector3(label, v).valueChanged) {
						value.value = v;
						return true;
					}
				}
			} else {
				if (isColor) {
					Engine::Color4 c{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2), ExtractFloatComponent(value, 3) };
					if (Engine::MyGUI::ColorEdit(label, c).valueChanged) {
						value.value = c;
						return true;
					}
				} else {
					Engine::Vector4 v{ ExtractFloatComponent(value, 0), ExtractFloatComponent(value, 1), ExtractFloatComponent(value, 2), ExtractFloatComponent(value, 3) };
					if (Engine::MyGUI::DragVector4(label, v).valueChanged) {
						value.value = v;
						return true;
					}
				}
			}
		} else if (var.valueType == D3D_SVT_INT) {
			int32_t v = std::visit([](const auto& val) -> int32_t {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, int32_t>) return val;
				else if constexpr (std::is_same_v<T, uint32_t>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, float>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, bool>) return val ? 1 : 0;
				else return 0;
			}, value.value);
			if (Engine::MyGUI::DragInt(label, v).valueChanged) {
				value.value = v;
				return true;
			}
		} else if (var.valueType == D3D_SVT_UINT) {
			int32_t iv = std::visit([](const auto& val) -> int32_t {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, uint32_t>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, int32_t>) return val;
				else if constexpr (std::is_same_v<T, float>) return static_cast<int32_t>(val);
				else if constexpr (std::is_same_v<T, bool>) return val ? 1 : 0;
				else return 0;
			}, value.value);
			if (Engine::MyGUI::DragInt(label, iv).valueChanged) {
				value.value = static_cast<uint32_t>((std::max)(0, iv));
				return true;
			}
		} else if (var.valueType == D3D_SVT_BOOL) {
			bool v = std::visit([](const auto& val) -> bool {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, bool>) return val;
				else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) return val != 0;
				else if constexpr (std::is_same_v<T, float>) return val != 0.0f;
				else return false;
			}, value.value);
			if (Engine::MyGUI::Checkbox(label, v)) {
				value.value = v;
				return true;
			}
		}
		return false;
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

	Engine::SceneHeader* ResolveActiveSceneHeader(const Engine::ToolContext& context) {

		// 通常はSceneInstanceManagerから実体のSceneHeaderを取得する
		if (context.sceneInstances && context.activeSceneInstanceID) {
			Engine::SceneInstance* activeScene = context.sceneInstances->Find(context.activeSceneInstanceID);
			if (activeScene) {
				return &activeScene->header;
			}
		}

		// 古い呼び出し経路でも反映できるように、実体を指すactiveSceneHeaderを最後の手段として使う
		return const_cast<Engine::SceneHeader*>(context.activeSceneHeader);
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
	if (!ImGui::Begin("PostProcessStack", &openWindow_)) {
		ImGui::End();
		return;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	service.EnsureLoaded();

	// 現在の設定ファイルパスと未保存マーカーを表示する
	{
		const std::string pathStr = service.GetCurrentPath().generic_string();
		if (service.IsDirty()) {
			ImGui::TextDisabled("Settings: %s *", pathStr.c_str());
		} else {
			ImGui::TextDisabled("Settings: %s", pathStr.c_str());
		}
	}

	// Save / Reload ボタン
	if (ImGui::Button("Save")) {
		service.Save();
		service.ClearDirty();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload")) {
		service.Reload();
		selectedPassIndex_ = -1;
	}

	ImGui::Separator();

	// .postProcessStack.json ドロップゾーン
	DrawDropZones(context);

	ImGui::Separator();

	// 左右カラム分割: 左=パス一覧、右=詳細
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

	ImGui::TextUnformatted("Passes");
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
		if (ImGui::Selectable(pass.name.empty() ? "(Unnamed)" : pass.name.c_str(),
			isSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f))) {
			selectedPassIndex_ = i;
		}

		// ドラッグソース: パスの並び替え
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
			ImGui::SetDragDropPayload(kPassReorderPayloadType, &i, sizeof(int32_t));
			ImGui::TextUnformatted(pass.name.empty() ? "(Unnamed)" : pass.name.c_str());
			ImGui::EndDragDropSource();
		}

		// ドラッグターゲット: ここへドロップで並び替える
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPassReorderPayloadType)) {
				reorderFrom = *static_cast<const int32_t*>(payload->Data);
				reorderTo = i;
			}
			ImGui::EndDragDropTarget();
		}

		// 右クリックコンテキストメニュー
		if (ImGui::BeginPopupContextItem("##PassContext")) {
			if (ImGui::MenuItem("Delete")) {
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
	PostProcessStackSettings& settings = service.GetSettings();
	auto& passes = settings.passes;

	if (selectedPassIndex_ < 0 || selectedPassIndex_ >= static_cast<int32_t>(passes.size())) {
		ImGui::TextDisabled("Select a pass to edit.");
		return;
	}

	PostProcessStackPassSettings& pass = passes[selectedPassIndex_];

	// 選択中パスをプレビュー対象としてPostProcessStackPassへ伝える
	service.SetPreviewPassId(pass.id);

	// 選択中パスのシェーダーを再コンパイルしてパラメータを再読み込みする
	if (ImGui::Button("Reload Reflection")) {
		PostProcessStackService::GetInstance().RequestShaderReload(pass.materialGuid);
	}

	ImGui::Separator();

	// パス名編集
	if (MyGUI::InputText("Name", pass.name).editFinished) {
		service.MarkDirty();
	}

	// パスシェーダーパス名
	if (MyGUI::InputText("Pass Name", pass.passName).editFinished) {
		service.MarkDirty();
		service.RebuildRuntime();
	}

	ImGui::Separator();

	// マテリアル参照フィールド
	{
		AssetID matGuid = pass.materialGuid;
		AssetEditSetting setting{};
		if (MyGUI::AssetReferenceField("Material", matGuid,
			context.toolContext.assetDatabase, { AssetType::Material }, setting).valueChanged) {

			pass.materialGuid = matGuid;
			service.MarkDirty();
			service.RebuildRuntime();
		}
	}

	ImGui::Separator();

	// CBuffer パラメータオーバーライドUI (Reflectionキャッシュ使用)
	const std::vector<ShaderConstantBufferVariable>* vars = service.FindReflectionVars(pass.materialGuid);
	if (vars && !vars->empty()) {

		ImGui::TextUnformatted("Parameters");
		ImGui::Separator();

		bool anyParamChanged = false;
		for (const auto& var : *vars) {

			// pad / padding はHLSLのアライメント調整用変数のため表示しない
			if (var.name.find("pad") != std::string::npos || var.name.find("Pad") != std::string::npos) {
				continue;
			}

			ImGui::PushID(var.name.c_str());

			auto it = pass.parameterOverrides.find(var.name);
			if (it == pass.parameterOverrides.end()) {

				// オーバーライドなし: デフォルト値を灰色で表示し、+ボタンでオーバーライドを追加する
				ImGui::TextDisabled("[default] %s", var.name.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("+##Override")) {
					pass.parameterOverrides[var.name] = DefaultValueForVariable(var);
					anyParamChanged = true;
				}
			} else {

				// オーバーライドあり: 値を編集可能に表示し、xボタンで削除できる
				if (DrawParameterValueEdit(var, it->second)) {
					anyParamChanged = true;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x##RemoveOverride")) {
					pass.parameterOverrides.erase(it);
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

		ImGui::TextUnformatted("Textures");
		ImGui::Separator();

		bool anySRVChanged = false;
		for (const auto& srv : *srvs) {

			ImGui::PushID(srv.name.c_str());

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

	ImGui::Separator();

	// 選択中パスの実行前後プレビュー(デフォルトは閉じておく)
	if (MyGUI::CollapsingHeader("Preview", false)) {

		const PostProcessStackService::PreviewImage& preview = service.GetPreviewImage();
		if (!preview.valid) {
			ImGui::TextDisabled("No preview available.");
		} else {

			// 表示できる範囲の幅から16:9でサイズを決める
			const float availWidth = ImGui::GetContentRegionAvail().x;
			const ImVec2 imageSize(availWidth, availWidth * 9.0f / 16.0f);

			ImGui::TextUnformatted("Before");
			ImGui::Image(static_cast<ImTextureID>(preview.beforeSrvPtr), imageSize);

			ImGui::TextUnformatted("After");
			ImGui::Image(static_cast<ImTextureID>(preview.afterSrvPtr), imageSize);
		}
	}
}

void Engine::PostProcessStackTool::DrawDropZones(const EditorToolContext& context) {

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	PostProcessStackSettings& settings = service.GetSettings();

	// ドロップゾーン: .postProcessStack.json を読み込む
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		const float zoneHeight = 24.0f;
		ImGui::Button("Drop .postProcessStack.json to load", ImVec2(ImGui::GetContentRegionAvail().x, zoneHeight));
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

	// ドロップゾーン: マテリアル / シェーダー / HLSL を追加する
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
		const float zoneHeight = 24.0f;
		ImGui::Button("Drop .material.json / .shader.json / .CS.hlsl to add PostProcess pass",
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

						// .material.json ドロップ: GUIDを直接取得する
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

						// .shader.json ドロップ: 対応するMaterialを検索または生成する
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

						// .CS.hlsl ドロップ: shader/pipeline/material を生成または検索する
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

						// 既存の material アセットパスキャッシュを解決する
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
						newPass.passName = "PostProcess";

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
		ImGui::OpenPopup(kUnsavedPopupId);
		pendingScenePathChange_ = false;
	}

	ImGui::SetNextWindowSize(ImVec2(340.0f, 120.0f), ImGuiCond_Always);
	if (!ImGui::BeginPopupModal(kUnsavedPopupId, nullptr,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
		return;
	}

	ImGui::TextUnformatted("PostProcessStack has unsaved changes.");
	ImGui::TextUnformatted("Switch scene without saving?");
	ImGui::Spacing();

	PostProcessStackService& service = PostProcessStackService::GetInstance();

	if (ImGui::Button("Save & Switch", ImVec2(110.0f, 0.0f))) {
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
	if (ImGui::Button("Discard & Switch", ImVec2(110.0f, 0.0f))) {
		service.SetActiveSettingsAsset(pendingNextStackAsset_, context.toolContext.assetDatabase);
		lastStackAsset_ = pendingNextStackAsset_;
		selectedPassIndex_ = -1;
		pendingScenePathChange_ = false;
		pendingNextStackAsset_ = {};
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) {
		pendingScenePathChange_ = false;
		pendingNextStackAsset_ = {};
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}
