#include "ParticleEffectEditorTool.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// 形状別
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticlePlaneShapeDrawer.h>
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticleCrossPlaneShapeDrawer.h>
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticleRingShapeDrawer.h>
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticleCylinderShapeDrawer.h>
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticleSphereShapeDrawer.h>
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticleHemisphereShapeDrawer.h>
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticleCubeShapeDrawer.h>

// c++
#include <algorithm>
#include <filesystem>
#include <optional>

//============================================================================
//	ParticleEffectEditorTool internal
//============================================================================
namespace {

	// float編集の共通設定
	using Engine::ParticleGui::MakeDragSetting;

	// モジュール並べ替えのドラッグ&ドロップペイロード
	constexpr const char* kModuleReorderPayloadType = "PARTICLE_MODULE_REORDER";
	// フェーズ並べ替えのドラッグ&ドロップペイロード
	constexpr const char* kPhaseReorderPayloadType = "PARTICLE_PHASE_REORDER";

	// リストの要素をfromからtoへ移動する
	template <typename T>
	void MoveListItem(std::vector<T>& list, int32_t from, int32_t to) {

		if (from < to) {
			std::rotate(list.begin() + from, list.begin() + from + 1, list.begin() + to + 1);
		} else {
			std::rotate(list.begin() + to, list.begin() + from, list.begin() + from + 1);
		}
	}

	bool IsPaddingName(const std::string& name) {

		return name.find("pad") != std::string::npos || name.find("Pad") != std::string::npos;
	}

	bool IsParticleMaterialAnimatable(const ShaderConstantBufferVariable& var) {

		if (!var.used || var.valueType != D3D_SVT_FLOAT || IsPaddingName(var.name)) {
			return false;
		}
		if (var.name == "color") {
			return false;
		}
		return true;
	}

	AssetID ResolvePhaseMaterialID(const ParticleEffectAsset& asset, const ParticleEffectPhase& phase) {

		if (phase.material) {
			return phase.material;
		}
		if (asset.material) {
			return asset.material;
		}
		return asset.space == PrimitiveRenderSpace::Screen2D ?
			BuiltinAssets::Materials::DefaultParticle2D : BuiltinAssets::Materials::DefaultParticle;
	}

	std::optional<MaterialAsset> LoadMaterialAsset(const EditorToolContext& context, AssetID materialID) {

		AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
		if (!assetDatabase || !materialID) {
			return std::nullopt;
		}
		const std::filesystem::path materialPath = assetDatabase->ResolveFullPath(materialID);
		if (materialPath.empty()) {
			return std::nullopt;
		}
		MaterialAsset material{};
		const nlohmann::json data = JsonAdapter::Load(materialPath.string(), false);
		if (!FromJson(data, material)) {
			return std::nullopt;
		}
		return material;
	}

	const ShaderReflectionInfo* FindParticleMaterialReflection(
		const EditorToolContext& context, const MaterialAsset& material) {

		if (!context.panelContext || !context.panelContext->renderPipeline) {
			return nullptr;
		}
		return context.panelContext->renderPipeline->FindMaterialDrawReflection(material);
	}

	bool ValidateParticleMaterialSelection(const EditorToolContext& context,
		AssetID materialID, std::string& outMessage) {

		outMessage.clear();
		if (!materialID) {
			return true;
		}
		const std::optional<MaterialAsset> material = LoadMaterialAsset(context, materialID);
		if (!material) {
			outMessage = "マテリアルを読み込めません";
			return false;
		}
		const ParticleMaterialCompatibilityResult compatibility = CheckParticleMaterialCompatibility(
			*material, FindParticleMaterialReflection(context, *material));
		if (compatibility.IsCompatible()) {
			return true;
		}
		if (compatibility.status == ParticleMaterialCompatibilityStatus::PendingReflection &&
			material->usage == MaterialUsage::Particle) {
			return true;
		}
		outMessage = compatibility.message;
		return false;
	}

	std::vector<ShaderConstantBufferVariable> CollectMaterialParameters(const ShaderReflectionInfo& reflection) {

		std::vector<ShaderConstantBufferVariable> variables{};
		if (const ShaderStructuredBufferInfo* buffer =
			FindStructuredBuffer(reflection, "gParticleCustomParameters")) {

			for (const ShaderConstantBufferVariable& var : buffer->variables) {
				if (IsParticleMaterialAnimatable(var)) {
					variables.emplace_back(var);
				}
			}
		}
		// 旧Particle PSはMaterialParametersからslotへ割り当てる
		if (!variables.empty()) {
			return variables;
		}
		for (const ShaderConstantBufferInfo& cb : reflection.constantBuffers) {
			if (cb.name != "MaterialParameters") {
				continue;
			}
			for (const ShaderConstantBufferVariable& var : cb.variables) {
				if (IsParticleMaterialAnimatable(var)) {
					variables.emplace_back(var);
				}
			}
		}
		std::sort(variables.begin(), variables.end(),
			[](const ShaderConstantBufferVariable& lhs, const ShaderConstantBufferVariable& rhs) {
				return lhs.name < rhs.name;
			});
		return variables;
	}

	std::vector<ShaderResourceBinding> CollectMaterialTextures(const ShaderReflectionInfo& reflection) {

		std::vector<ShaderResourceBinding> textures{};
		for (const ShaderResourceBinding& resource : reflection.resources) {
			if (resource.kind == ShaderBindingKind::SRV && resource.space == 2 &&
				resource.rawType == D3D_SIT_TEXTURE && resource.name != "baseColorTexture") {
				textures.emplace_back(resource);
			}
		}
		std::sort(textures.begin(), textures.end(),
			[](const ShaderResourceBinding& lhs, const ShaderResourceBinding& rhs) {
				return lhs.name < rhs.name;
			});
		return textures;
	}
}

//============================================================================
//	ParticleEffectEditorTool classMethods
//============================================================================
void ParticleEffectEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void ParticleEffectEditorTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		return;
	}
	DrawWindow(context);
}

void ParticleEffectEditorTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("ParticleEffect", &openWindow_)) {

		ImGui::End();
		return;
	}
	ImGui::SetWindowFontScale(0.8f);

	DrawAssetSection(context);

	if (loaded_) {

		// 変更検知フラグ
		bool changed = false;

		if (ImGui::BeginTabBar("ParticleEffectEditorToolTabBar")) {

			changed |= DrawBasicSection(context);
			changed |= DrawPhaseSection(context);

			ImGui::EndTabBar();
		}

		// 変更があった場合にランタイムに適用する
		if (changed) {
			ApplyToRuntime();
		}
	}

	if (!statusMessage_.empty()) {
		ImGui::TextWrapped("%s", statusMessage_.c_str());
	}

	ImGui::SetWindowFontScale(1.0f);
	ImGui::End();
}

void ParticleEffectEditorTool::DrawAssetSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;

	// 編集対象のエフェクトを選択する
	AssetID selected = editingID_;
	AssetEditSetting setting{};
	setting.defaultAssetID = BuiltinAssets::Effects::DefaultParticle;
	if (MyGUI::AssetReferenceField("エフェクト", selected,
		assetDatabase, { AssetType::ParticleEffect }, setting).valueChanged) {

		LoadEffect(context, selected);
	}

	// 新規作成、GameAssets/Effects配下へ作成する
	ImGui::Separator();
	MyGUI::InputText("GameAssets/Effects/", createNameBuffer_);
	const bool canCreate = !createNameBuffer_.empty();
	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("新規作成")) {
		CreateEffect(context);
	}
	ImGui::EndDisabled();

	// 保存、編集は保存前でも即シーンへ反映される
	ImGui::SameLine();
	ImGui::BeginDisabled(!loaded_);
	if (ImGui::Button("保存")) {
		SaveEffect(context);
	}
	ImGui::EndDisabled();
	ImGui::Separator();
}

bool ParticleEffectEditorTool::DrawBasicSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	bool changed = false;

	//============================================================================
	//	エミッター編集
	//============================================================================
	if (ImGui::BeginTabItem("エミッター")) {

		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("発生設定", false)) {

			changed |= MyGUI::DragFloat("発生間隔", draft_.emitter.emitInterval, MakeDragSetting(0.001f, 60.0f)).valueChanged;
			changed |= ParticleGui::DrawParticleValueUInt("発生数", draft_.emitter.emitCount);
			{
				int32_t maxParticles = static_cast<int32_t>(draft_.emitter.maxParticles);
				if (MyGUI::DragInt("最大数", maxParticles).valueChanged) {

					draft_.emitter.maxParticles = static_cast<uint32_t>((std::max)(1, maxParticles));
					changed = true;
				}
				// 現在の発生数を集計して表示する
				uint32_t aliveCount = 0;
				if (ECSWorld* world = context.GetWorld()) {
					world->ForEach<ParticleEmitterComponent>([&](const Entity&, const ParticleEmitterComponent& component) {
						const AssetID resolved = component.effect ? component.effect : BuiltinAssets::Effects::DefaultParticle;
						if (resolved == editingID_) {
							aliveCount += static_cast<uint32_t>(component.runtimeParticles.size());
						}
						});
				}
				ImGui::Text("現在の発生数: %u / %u", aliveCount, draft_.emitter.maxParticles);
			}
			ImGui::Spacing();

			changed |= ParticleGui::DrawParticleValueFloat("発生初速度", draft_.emitter.speed, MakeDragSetting(0.0f, 10000.0f));
			changed |= ParticleGui::DrawParticleValueVector3("発生オフセット", draft_.emitter.emitOffset, MakeDragSetting(-10000.0f, 10000.0f));

			ImGui::Spacing();
			ImGui::Separator();

			changed |= MyGUI::DragFloat("発生継続時間", draft_.duration, MakeDragSetting(0.01f, 600.0f)).valueChanged;
			changed |= MyGUI::Checkbox("ループ再生", draft_.looping);

			// シーン上の対象エミッターをまとめて再生制御する、単発はループを無視して1回だけ発生する
			if (ImGui::Button("再生")) {
				RestartEmitters(context, false);
			}
			ImGui::SameLine();
			if (ImGui::Button("単発再生")) {
				RestartEmitters(context, true);
			}
			ImGui::SameLine();
			if (ImGui::Button("停止")) {
				StopEmitters(context);
			}
		}
		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("エミッター形状設定", false)) {

			// 空間で使える形状だけを選択候補にする
			const bool is2D = draft_.space == PrimitiveRenderSpace::Screen2D;
			ParticleEmitterShapeRegistry& shapeRegistry = ParticleEmitterShapeRegistry::GetInstance();
			const std::vector<ParticleEmitterShape> shapes = shapeRegistry.GetShapes(is2D);

			// 空間に合わない形状は先頭へフォールバック
			int32_t currentIndex = 0;
			bool found = false;
			for (int32_t i = 0; i < static_cast<int32_t>(shapes.size()); ++i) {
				if (shapes[i] == draft_.emitter.shape) { currentIndex = i; found = true; break; }
			}
			if (!found && !shapes.empty()) {

				draft_.emitter.shape = shapes.front();
				changed = true;
			}

			if (!shapes.empty() &&
				ImGui::BeginCombo("発生形状", EnumAdapter<ParticleEmitterShape>::ToString(shapes[currentIndex]))) {
				for (int32_t i = 0; i < static_cast<int32_t>(shapes.size()); ++i) {
					if (ImGui::Selectable(EnumAdapter<ParticleEmitterShape>::ToString(shapes[i]), i == currentIndex)) {

						draft_.emitter.shape = shapes[i];
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			// 形状別パラメータ
			if (const IParticleEmitterShape* shape = shapeRegistry.Find(draft_.emitter.shape)) {
				changed |= shape->DrawImGui(draft_.emitter);
			}
		}
		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("描画設定", false)) {

			changed |= MyGUI::EnumCombo("描画空間", draft_.space).valueChanged;
			changed |= MyGUI::EnumCombo("形状", draft_.shape).valueChanged;

			// 形状ごとのパラメータ
			if (const IParticlePrimitiveShapeDrawer* drawer =
				ParticlePrimitiveShapeDrawerRegistry::GetInstance().Find(draft_.shape)) {
				changed |= drawer->DrawImGui(draft_);
			}
			// 描画設定
			{
				AssetEditSetting setting{};
				changed |= MyGUI::AssetReferenceField("モデル", draft_.model, assetDatabase, { AssetType::Mesh }, setting).valueChanged;
			}
			{
				AssetEditSetting setting{};
				setting.defaultAssetID = draft_.space == PrimitiveRenderSpace::Screen2D ?
					BuiltinAssets::Materials::DefaultParticle2D : BuiltinAssets::Materials::DefaultParticle;
				AssetID material = draft_.material;
				if (MyGUI::AssetReferenceField("マテリアル", material,
					assetDatabase, { AssetType::Material }, setting).valueChanged) {

					std::string message{};
					if (ValidateParticleMaterialSelection(context, material, message)) {
						draft_.material = material;
						changed = true;
						statusMessage_.clear();
					} else {
						statusMessage_ = "Particleマテリアルを設定できません: " + message;
					}
				}
			}
			changed |= MyGUI::EnumCombo("ソート", draft_.sortMode).valueChanged;
			changed |= MyGUI::EnumCombo("ブレンドモード", draft_.blendMode).valueChanged;
			changed |= MyGUI::EnumCombo("キュー", draft_.queue).valueChanged;
			// ビルボード軸
			{
				const Axis axes[] = { Axis::X, Axis::Y, Axis::Z };
				const char* axisLabels[] = { "ビルボードX", "ビルボードY", "ビルボードZ" };
				for (int32_t i = 0; i < 3; ++i) {

					bool enabled = std::find(draft_.billboardAxes.begin(), draft_.billboardAxes.end(), axes[i]) != draft_.billboardAxes.end();
					if (MyGUI::Checkbox(axisLabels[i], enabled)) {

						if (enabled) {
							draft_.billboardAxes.emplace_back(axes[i]);
						} else {
							std::erase(draft_.billboardAxes, axes[i]);
						}
						changed = true;
					}
				}
			}

			ImGui::SeparatorText("トレイル");
			changed |= MyGUI::Checkbox("トレイル描画", draft_.trail.enabled);
			if (draft_.trail.enabled) {

				changed |= MyGUI::DragInt("軌跡点の上限", draft_.trail.maxPoints).valueChanged;
				changed |= MyGUI::DragFloat("最小移動距離", draft_.trail.minDistance, MakeDragSetting(0.001f, 100.0f)).valueChanged;
				changed |= MyGUI::DragFloat("先頭の幅", draft_.trail.startWidth, MakeDragSetting(0.001f, 100.0f)).valueChanged;
				changed |= MyGUI::DragFloat("尻尾の幅", draft_.trail.endWidth, MakeDragSetting(0.001f, 100.0f)).valueChanged;
				changed |= MyGUI::ColorEdit("先頭の色", draft_.trail.startColor).valueChanged;
				changed |= MyGUI::ColorEdit("尻尾の色", draft_.trail.endColor).valueChanged;
				changed |= MyGUI::DragFloat("点の寿命", draft_.trail.pointLifetime, MakeDragSetting(0.0f, 60.0f)).valueChanged;
				{
					// 未設定なら粒子と同じマテリアルを使う
					AssetEditSetting setting{};
					changed |= MyGUI::AssetReferenceField("トレイルマテリアル", draft_.trail.material,
						assetDatabase, { AssetType::Material }, setting).valueChanged;
				}
			}
		}
		ImGui::EndTabItem();
	}
	return changed;
}

bool ParticleEffectEditorTool::DrawPhaseSection(const EditorToolContext& context) {

	bool changed = false;

	//============================================================================
	//	フェーズ編集
	//============================================================================
	if (ImGui::BeginTabItem("フェーズ")) {

		// フェーズは必ず1つ以上持つ
		if (draft_.phases.empty()) {

			draft_.phases.emplace_back();
			changed = true;
		}
		// 編集用インスタンスの数をフェーズ数へ合わせる
		if (moduleCache_.size() != draft_.phases.size()) {
			moduleCache_.resize(draft_.phases.size());
		}
		if (selectedModules_.size() != draft_.phases.size()) {
			selectedModules_.resize(draft_.phases.size(), 0);
		}
		selectedPhase_ = std::clamp(selectedPhase_, 0, static_cast<int32_t>(draft_.phases.size()) - 1);

		//========================================================================================================================================================
		// 左のフェーズリスト、選択と追加と削除と並べ替え
		ImGui::BeginChild("PhaseList", ImVec2(160.0f, 0.0f), true);
		if (ImGui::Button("追加", ImVec2(-FLT_MIN, 0.0f))) {

			ParticleEffectPhase phase{};
			phase.name = "Phase " + std::to_string(draft_.phases.size() + 1);
			draft_.phases.emplace_back(std::move(phase));
			moduleCache_.emplace_back();
			selectedModules_.emplace_back(0);
			selectedPhase_ = static_cast<int32_t>(draft_.phases.size()) - 1;
			changed = true;
		}
		ImGui::Separator();
		int32_t removePhaseIndex = -1;
		for (int32_t i = 0; i < static_cast<int32_t>(draft_.phases.size()); ++i) {

			ImGui::PushID(i);
			const std::string label = std::to_string(i + 1) + ": " + draft_.phases[i].name;
			if (ImGui::Selectable(label.c_str(), i == selectedPhase_)) {
				selectedPhase_ = i;
			}
			// ドラッグ&ドロップで並べ替える
			if (ImGui::BeginDragDropSource()) {

				ImGui::SetDragDropPayload(kPhaseReorderPayloadType, &i, sizeof(i));
				ImGui::TextUnformatted(label.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget()) {

				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPhaseReorderPayloadType)) {

					const int32_t from = *static_cast<const int32_t*>(payload->Data);
					if (from != i) {

						MoveListItem(draft_.phases, from, i);
						MoveListItem(moduleCache_, from, i);
						MoveListItem(selectedModules_, from, i);
						// 選択中のフェーズを追従させる
						if (from == selectedPhase_) {
							selectedPhase_ = i;
						} else if (from < selectedPhase_ && selectedPhase_ <= i) {
							--selectedPhase_;
						} else if (i <= selectedPhase_ && selectedPhase_ < from) {
							++selectedPhase_;
						}
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			// 右クリックで削除、最後の1つは消せない
			if (ImGui::BeginPopupContextItem()) {

				if (ImGui::MenuItem("削除", nullptr, false, 1 < draft_.phases.size())) {
					removePhaseIndex = i;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		if (0 <= removePhaseIndex) {

			draft_.phases.erase(draft_.phases.begin() + removePhaseIndex);
			moduleCache_.erase(moduleCache_.begin() + removePhaseIndex);
			selectedModules_.erase(selectedModules_.begin() + removePhaseIndex);
			selectedPhase_ = std::clamp(selectedPhase_, 0, static_cast<int32_t>(draft_.phases.size()) - 1);
			changed = true;
		}
		ImGui::EndChild();

		//========================================================================================================================================================
		// 右の選択フェーズ編集
		ImGui::SameLine();
		ImGui::BeginChild("PhaseEdit", ImVec2(0.0f, 0.0f));

		ParticleEffectPhase& phase = draft_.phases[selectedPhase_];
		changed |= MyGUI::InputText("名前", phase.name).valueChanged;
		changed |= ParticleGui::DrawParticleValueFloat("寿命", phase.lifetime, MakeDragSetting(0.001f, 600.0f));
		changed |= MyGUI::EnumCombo("寿命終了時", phase.lifeEndMode).valueChanged;
		{
			// 未設定ならエフェクト共通のマテリアルを引き継ぐ
			AssetEditSetting setting{};
			AssetID material = phase.material;
			if (MyGUI::AssetReferenceField("マテリアル", material,
				context.toolContext.assetDatabase, { AssetType::Material }, setting).valueChanged) {

				std::string message{};
				if (ValidateParticleMaterialSelection(context, material, message)) {
					phase.material = material;
					changed = true;
					statusMessage_.clear();
				} else {
					statusMessage_ = "Particleマテリアルを設定できません: " + message;
				}
			}
		}

		changed |= DrawPhaseMaterialSection(context, phase);

		ImGui::SeparatorText("モジュール");
		changed |= DrawPhaseModules(context, phase);
		ImGui::EndChild();

		ImGui::EndTabItem();
	}
	return changed;
}

bool ParticleEffectEditorTool::DrawPhaseMaterialSection(const EditorToolContext& context,
	ParticleEffectPhase& phase) {

	bool changed = false;
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	ParticlePhaseMaterialSettings& materialSettings = phase.materialSettings;

	if (!MyGUI::CollapsingHeader("テクスチャ設定", false)) {
		return false;
	}
	{
		AssetEditSetting setting{};
		changed |= MyGUI::AssetReferenceField("ベースカラーテクスチャ", materialSettings.baseColorTexture,
			assetDatabase, { AssetType::Texture }, setting).valueChanged;
	}

	const AssetID materialID = ResolvePhaseMaterialID(draft_, phase);
	const std::optional<MaterialAsset> material = LoadMaterialAsset(context, materialID);
	if (!material) {
		ImGui::TextDisabled("マテリアルを解決できません");
		return changed;
	}
	const ShaderReflectionInfo* reflection = FindParticleMaterialReflection(context, *material);
	if (!reflection) {
		ImGui::TextDisabled("シェーダーリフレクションを取得できません");
		return changed;
	}

	const std::vector<ShaderResourceBinding> textures = CollectMaterialTextures(*reflection);
	if (!textures.empty()) {

		for (const ShaderResourceBinding& texture : textures) {

			ImGui::PushID(texture.name.c_str());
			AssetID textureID{};
			if (auto it = materialSettings.textureOverrides.find(texture.name);
				it != materialSettings.textureOverrides.end()) {
				textureID = it->second;
			}
			AssetEditSetting setting{};
			if (MyGUI::AssetReferenceField(texture.name.c_str(), textureID,
				assetDatabase, { AssetType::Texture }, setting).valueChanged) {

				if (textureID) {
					materialSettings.textureOverrides[texture.name] = textureID;
				} else {
					materialSettings.textureOverrides.erase(texture.name);
				}
				changed = true;
			}
			ImGui::PopID();
		}
	}

	return changed;
}

bool ParticleEffectEditorTool::DrawPhaseModules(const EditorToolContext& context, ParticleEffectPhase& phase) {

	bool changed = false;
	std::vector<ModuleCacheEntry>& cache = moduleCache_[selectedPhase_];
	if (cache.size() != phase.modules.size()) {
		cache.resize(phase.modules.size());
	}
	int32_t& selectedModule = selectedModules_[selectedPhase_];
	selectedModule = phase.modules.empty() ? -1 :
		std::clamp(selectedModule, 0, static_cast<int32_t>(phase.modules.size()) - 1);

	ImGui::BeginChild("ModuleList", ImVec2(190.0f, 0.0f), true);
	const std::vector<std::string> registeredIDs = ParticleModuleRegistry::GetInstance().GetRegisteredIDs();
	if (!registeredIDs.empty()) {

		addModuleIndex_ = std::clamp(addModuleIndex_, 0, static_cast<int32_t>(registeredIDs.size()) - 1);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##AddModule", registeredIDs[addModuleIndex_].c_str())) {
			for (int32_t i = 0; i < static_cast<int32_t>(registeredIDs.size()); ++i) {
				if (ImGui::Selectable(registeredIDs[i].c_str(), i == addModuleIndex_)) {
					addModuleIndex_ = i;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("追加", ImVec2(-FLT_MIN, 0.0f))) {

			ParticleEffectModuleEntry entry{};
			entry.id = registeredIDs[addModuleIndex_];
			phase.modules.emplace_back(std::move(entry));
			cache.emplace_back();
			selectedModule = static_cast<int32_t>(phase.modules.size()) - 1;
			changed = true;
		}
	}
	ImGui::Separator();
	int32_t removeIndex = -1;
	for (int32_t i = 0; i < static_cast<int32_t>(phase.modules.size()); ++i) {

		ParticleEffectModuleEntry& entry = phase.modules[i];
		ImGui::PushID(i);
		if (ImGui::Selectable(entry.id.c_str(), i == selectedModule)) {
			selectedModule = i;
		}
		if (ImGui::BeginDragDropSource()) {

			ImGui::SetDragDropPayload(kModuleReorderPayloadType, &i, sizeof(i));
			ImGui::TextUnformatted(entry.id.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModuleReorderPayloadType)) {

				const int32_t from = *static_cast<const int32_t*>(payload->Data);
				if (from != i) {

					MoveListItem(phase.modules, from, i);
					MoveListItem(cache, from, i);
					if (selectedModule == from) {
						selectedModule = i;
					} else if (from < selectedModule && selectedModule <= i) {
						--selectedModule;
					} else if (i <= selectedModule && selectedModule < from) {
						++selectedModule;
					}
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem()) {

			if (ImGui::MenuItem("削除")) {
				removeIndex = i;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("ModuleEdit", ImVec2(0.0f, 0.0f), true);
	if (0 <= selectedModule && selectedModule < static_cast<int32_t>(phase.modules.size())) {

		ParticleEffectModuleEntry& entry = phase.modules[selectedModule];

		// モジュール名表示
		std::string moduleName = "モジュール名: " + entry.id;
		ImGui::Text(moduleName.c_str());
		ImGui::Separator();
		// キャッシュされたモジュールリストの中から選択IDで引く
		if (IParticleModule* module = ResolveModuleCache(cache[selectedModule], entry)) {
			if (auto* custom = dynamic_cast<ParticleCustomShaderParameterModule*>(module)) {

				std::vector<ShaderConstantBufferVariable> parameters{};
				const AssetID materialID = ResolvePhaseMaterialID(draft_, phase);
				if (const std::optional<MaterialAsset> material = LoadMaterialAsset(context, materialID)) {
					if (const ShaderReflectionInfo* reflection = FindParticleMaterialReflection(context, *material)) {
						parameters = CollectMaterialParameters(*reflection);
					}
				}
				custom->SetReflectedParameters(parameters);
			}
			if (module->DrawImGui()) {

				entry.params = module->ToJson();
				changed = true;
			}
		} else {
			ImGui::TextDisabled("未登録のモジュールです");
		}
	} else {
		ImGui::TextDisabled("モジュールを選択してください");
	}
	ImGui::EndChild();

	if (0 <= removeIndex) {

		phase.modules.erase(phase.modules.begin() + removeIndex);
		cache.erase(cache.begin() + removeIndex);
		if (phase.modules.empty()) {
			selectedModule = -1;
		} else if (selectedModule >= static_cast<int32_t>(phase.modules.size())) {
			selectedModule = static_cast<int32_t>(phase.modules.size()) - 1;
		}
		changed = true;
	}
	return changed;
}

void ParticleEffectEditorTool::RestartEmitters(const EditorToolContext& context, bool oneShot) {

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return;
	}
	// 対象エフェクトを使っているエミッターを頭から再生する
	world->ForEach<ParticleEmitterComponent>([&](const Entity&, ParticleEmitterComponent& component) {

		const AssetID resolved = component.effect ? component.effect : BuiltinAssets::Effects::DefaultParticle;
		if (resolved != editingID_) {
			return;
		}
		component.playing = true;
		component.runtimeOneShot = oneShot;
		component.runtimeTime = 0.0f;
		component.runtimeEmitTimer = 0.0f;
		component.runtimeParticles.clear();
		component.runtimeTrails.clear();
		});
}

void ParticleEffectEditorTool::StopEmitters(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return;
	}
	// 対象エフェクトを使っているエミッターを停止して粒子を消す
	world->ForEach<ParticleEmitterComponent>([&](const Entity&, ParticleEmitterComponent& component) {

		const AssetID resolved = component.effect ? component.effect : BuiltinAssets::Effects::DefaultParticle;
		if (resolved != editingID_) {
			return;
		}
		component.playing = false;
		component.runtimeOneShot = false;
		component.runtimeParticles.clear();
		component.runtimeTrails.clear();
		});
}

Engine::IParticleModule* ParticleEffectEditorTool::ResolveModuleCache(ModuleCacheEntry& cache, const ParticleEffectModuleEntry& entry) {

	// idが変わっていたら作り直し、現在のパラメータを読み込ませる
	if (!cache.module || cache.id != entry.id) {

		cache.id = entry.id;
		cache.module = ParticleModuleRegistry::GetInstance().Create(entry.id);
		if (cache.module) {
			cache.module->FromJson(entry.params);
		}
	}
	return cache.module.get();
}

void ParticleEffectEditorTool::LoadEffect(const EditorToolContext& context, AssetID effectID) {

	loaded_ = false;
	editingID_ = effectID;
	moduleCache_.clear();
	selectedPhase_ = 0;
	if (!effectID || !context.toolContext.assetDatabase) {
		return;
	}

	const std::filesystem::path path = context.toolContext.assetDatabase->ResolveFullPath(effectID);
	if (path.empty()) {

		statusMessage_ = "エフェクトファイルが見つかりません";
		return;
	}
	const nlohmann::json data = JsonAdapter::Load(path.string(), false);
	if (!FromJson(data, draft_)) {

		statusMessage_ = "エフェクトファイルの読み込みに失敗しました";
		return;
	}
	loaded_ = true;
	statusMessage_.clear();
}

void ParticleEffectEditorTool::SaveEffect(const EditorToolContext& context) {

	if (!loaded_ || !editingID_ || !context.toolContext.assetDatabase) {
		return;
	}

	const std::filesystem::path path = context.toolContext.assetDatabase->ResolveFullPath(editingID_);
	if (path.empty()) {

		statusMessage_ = "保存先のパスを解決できません";
		return;
	}
	JsonAdapter::Save(path.string(), ToJson(draft_));
	statusMessage_ = "保存しました: " + path.filename().string();
}

void ParticleEffectEditorTool::CreateEffect(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	if (!assetDatabase || createNameBuffer_.empty()) {
		return;
	}

	// 既定のフェーズ構成で新規エフェクトを作る
	ParticleEffectAsset asset{};
	asset.name = createNameBuffer_;
	ParticleEffectPhase phase{};
	phase.name = "Phase 1";
	phase.modules = {
		{ "SizeOverLifetime", nlohmann::json::object() },
		{ "ColorOverLifetime", nlohmann::json::object() },
	};
	asset.phases.emplace_back(std::move(phase));

	const std::string logical = "GameAssets/Effects/" + createNameBuffer_ + ".effect.json";
	const std::filesystem::path path = assetDatabase->ResolveAssetPath(logical);
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	JsonAdapter::Save(path.string(), ToJson(asset));

	const AssetID assetID = assetDatabase->ImportOrGet(logical, AssetType::ParticleEffect);
	if (!assetID) {

		statusMessage_ = "エフェクトの作成に失敗しました";
		return;
	}
	statusMessage_ = "作成しました: " + logical;
	LoadEffect(context, assetID);
}

void ParticleEffectEditorTool::ApplyToRuntime() {

	if (!loaded_ || !editingID_) {
		return;
	}
	ParticleEffectEditBridge::GetInstance().Push(editingID_, draft_);
}
