#include "ParticleEffectEditorTool.h"
#include "ParticleEditorDescriptorRegistry.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailSizeOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailColorOverLifetimeModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailColorUVModule.h>
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleTrailCustomShaderParameterModule.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
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
	// グループ並べ替えのドラッグ&ドロップペイロード
	constexpr const char* kGroupReorderPayloadType = "PARTICLE_GROUP_REORDER";
	// コンポーネント設定が対象エフェクトを参照しているか
	bool UsesEffect(const Engine::ParticleSystemComponent& component,
		Engine::AssetID effectID) {

		const Engine::AssetID resolved = component.effect ? component.effect :
			Engine::BuiltinAssets::Effects::DefaultParticle;
		return resolved == effectID;
	}

	// 対象コンポーネントの実行状態を取得する
	const Engine::ParticleEffectInstanceRuntime* ResolveEffectInstance(
		const Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::ParticleSystemComponent& component,
		Engine::AssetID effectID) {

		if (!UsesEffect(component, effectID)) {
			return nullptr;
		}
		const Engine::ParticleSystemRuntimeData* runtime =
			Engine::TryGetParticleSystemRuntime(world, entity);
		return runtime ? &runtime->effect : nullptr;
	}

	// 親localFileIDから表示名を作る
	std::string MakeParticleParentLabel(Engine::ECSWorld* world, Engine::UUID target) {

		if (!target) {
			return "なし";
		}
		if (!world) {
			return Engine::ToString(target);
		}
		const Engine::Entity entity = Engine::SceneObjectUtility::FindByLocalFileID(*world, target);
		if (!entity.IsValid() || !world->IsAlive(entity)) {
			return "不明 : " + Engine::ToString(target);
		}
		if (world->HasComponent<Engine::NameComponent>(entity)) {
			return world->GetComponent<Engine::NameComponent>(entity).name;
		}
		return Engine::ToString(target);
	}

	// 親エンティティ参照欄、ヒエラルキーからドロップしてlocalFileIDを設定する
	Engine::ValueEditResult DrawParticleParentField(const char* label,
		Engine::ECSWorld* world, Engine::UUID& target) {

		using namespace Engine;
		ValueEditResult result{};
		if (!MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		const float clearWidth = 56.0f;
		const std::string buttonLabel = MakeParticleParentLabel(world, target);
		ImGui::Button(buttonLabel.c_str(),
			ImVec2((std::max)(0.0f, ImGui::GetContentRegionAvail().x - clearWidth), 0.0f));
		result.anyItemActive |= ImGui::IsItemActive();

		if (world && ImGui::BeginDragDropTarget()) {

			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType);
			if (payload && payload->DataSize == sizeof(Engine::UUID)) {

				const Engine::UUID droppedUUID = *static_cast<const Engine::UUID*>(payload->Data);
				const Entity dropped = world->FindByUUID(droppedUUID);
				Engine::UUID localFileID{};
				if (dropped.IsValid() && world->HasComponent<SceneObjectComponent>(dropped)) {
					localFileID = world->GetComponent<SceneObjectComponent>(dropped).localFileID;
				}
				if (localFileID && target != localFileID) {

					target = localFileID;
					result.valueChanged = true;
					result.editFinished = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		if (ImGui::Button("クリア", ImVec2(clearWidth, 0.0f)) && target) {

			target = Engine::UUID{};
			result.valueChanged = true;
			result.editFinished = true;
		}
		MyGUI::EndPropertyRow();
		return result;
	}

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
		if (var.name == MaterialParameterNames::BaseColor) {
			return false;
		}
		return true;
	}

	AssetID ResolvePhaseMaterialID(const ParticleEffectAsset& asset,
		const ParticleEffectGroup& group, const ParticleEffectPhase& phase) {

		if (phase.material) {
			return phase.material;
		}
		if (group.material) {
			return group.material;
		}
		return asset.space == PrimitiveRenderSpace::Screen2D ?
			BuiltinAssets::Materials::DefaultParticle2D : BuiltinAssets::Materials::DefaultParticle;
	}

	AssetID ResolveTrailMaterialID(const ParticleEffectAsset& asset, const ParticleEffectGroup& group) {

		if (group.trail.material) {
			return group.trail.material;
		}
		if (group.material) {
			return group.material;
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
				resource.rawType == D3D_SIT_TEXTURE &&
				resource.name != MaterialParameterNames::BaseColorTexture) {
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

void ParticleEffectEditorTool::OpenAsset(AssetID assetID) {

	pendingAsset_ = assetID;
	openWindow_ = true;
}

void ParticleEffectEditorTool::DrawEditorTool(const EditorToolContext& context) {

	if (pendingAsset_) {
		LoadEffect(context, pendingAsset_);
		pendingAsset_ = {};
	}
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
	if (!statusMessage_.empty()) {
		ImGui::TextWrapped("%s", statusMessage_.c_str());
	}

	if (loaded_) {

		// 変更検知フラグ
		bool changed = false;
		if (draft_.groups.empty()) {

			draft_.groups.emplace_back();
			selectedGroupID_ = draft_.groups.front().id;
			changed = true;
		}
		changed |= DrawGroupEmissionSection(context);
		ImGui::BeginChild("ParticleEffectGroupList", ImVec2(160.0f, 0.0f), true);
		changed |= DrawGroupList();
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("ParticleEffectGroupEdit", ImVec2(0.0f, 0.0f));
		if (ParticleEffectGroup* group = GetSelectedGroup()) {

			GroupEditorState& editorState = GetGroupEditorState(group->id);
			if (ImGui::BeginTabBar("ParticleEffectEditorToolTabBar")) {

				changed |= DrawBasicSection(context, *group);
				changed |= DrawPhaseSection(context, *group, editorState);
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();

		// 変更があった場合にランタイムに適用する
		if (changed) {
			ApplyToRuntime();
		}
	}

	ImGui::SetWindowFontScale(1.0f);
	ImGui::End();
}

bool ParticleEffectEditorTool::DrawGroupEmissionSection(const EditorToolContext& context) {

	bool changed = false;
	if (MyGUI::CollapsingHeader("グループ発生設定", false)) {
		MyGUI::ScopedPropertyLabelWidth labelWidth("GroupEmissionSettings");

		bool playing = false;
		bool oneShot = false;
		float currentInterval = 0.0f;
		bool foundSystem = false;
		if (ECSWorld* world = context.GetWorld()) {
			world->ForEach<ParticleSystemComponent>(
				[&](const Entity& entity, const ParticleSystemComponent& component) {

					const ParticleEffectInstanceRuntime* effect = ResolveEffectInstance(
						*world, entity, component, editingID_);
					if (!effect) {
						return;
					}
					if (!foundSystem) {
						currentInterval = effect->runtimeGroupEmitTimer;
						foundSystem = true;
					}
					playing |= !effect->emissionStopped;
					oneShot |= effect->oneShot;
				});
		}
		if (MyGUI::BeginPropertyRow("再生状態")) {

			ImGui::TextUnformatted(oneShot ? "単発再生中" : (playing ? "再生中" : "停止中"));
			MyGUI::EndPropertyRow();
		}

		if (MyGUI::BeginPropertyRow("発生方法")) {

			const char* labels[] = { "個別発生", "同時発生" };
			int32_t current = static_cast<int32_t>(draft_.groupEmission.mode);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			if (ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels))) {

				draft_.groupEmission.mode = static_cast<ParticleEffectGroupEmissionMode>(current);
				changed = true;
			}
			MyGUI::EndPropertyRow();
		}
		if (draft_.groupEmission.mode == ParticleEffectGroupEmissionMode::Simultaneous) {

			changed |= MyGUI::Checkbox("全グループの終了を待つ", draft_.groupEmission.waitForCompletion);
			changed |= MyGUI::DragFloat("同時発生間隔", draft_.groupEmission.interval,
				MakeDragSetting(0.0f, 60.0f)).valueChanged;
			if (MyGUI::BeginPropertyRow("現在の発生間隔")) {

				ImGui::Text("%.3f / %.3f", currentInterval, draft_.groupEmission.interval);
				MyGUI::EndPropertyRow();
			}
		}
	}
	if (ImGui::Button("再生")) {
		RestartParticleSystems(context, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("単発再生")) {
		RestartParticleSystems(context, true);
	}
	ImGui::SameLine();
	if (ImGui::Button("停止")) {
		StopParticleSystems(context);
	}
	ImGui::Separator();
	return changed;
}

bool ParticleEffectEditorTool::DrawGroupList() {

	bool changed = false;
	if (ImGui::Button("追加", ImVec2(-FLT_MIN, 0.0f))) {

		ParticleEffectGroup group{};
		group.name = "Group " + std::to_string(draft_.groups.size() + 1);
		ParticleEffectPhase phase{};
		phase.name = "Phase 1";
		phase.modules = {
			{ "SizeOverLifetime", nlohmann::json::object() },
			{ "ColorOverLifetime", nlohmann::json::object() },
		};
		group.phases.emplace_back(std::move(phase));
		draft_.groups.emplace_back(std::move(group));
		selectedGroupID_ = draft_.groups.back().id;
		changed = true;
	}
	ParticleEffectGroup* selected = GetSelectedGroup();
	ImGui::BeginDisabled(!selected);
	if (ImGui::Button("複製", ImVec2(-FLT_MIN, 0.0f)) && selected) {

		ParticleEffectGroup copy = *selected;
		copy.id = UUID::New();
		copy.name += " Copy";
		draft_.groups.emplace_back(std::move(copy));
		selectedGroupID_ = draft_.groups.back().id;
		changed = true;
	}
	ImGui::EndDisabled();
	ImGui::Separator();

	int32_t removeIndex = -1;
	for (int32_t i = 0; i < static_cast<int32_t>(draft_.groups.size()); ++i) {

		ParticleEffectGroup& group = draft_.groups[i];
		const std::string groupID = ToString(group.id);
		ImGui::PushID(groupID.c_str());
		if (MyGUI::SmallCheckbox("##Enabled", group.enabled)) { changed = true; }
		ImGui::SameLine();
		const std::string label = std::to_string(i + 1) + ": " + group.name;
		if (ImGui::Selectable(label.c_str(), group.id == selectedGroupID_)) {
			selectedGroupID_ = group.id;
		}
		if (ImGui::BeginDragDropSource()) {

			ImGui::SetDragDropPayload(kGroupReorderPayloadType, &i, sizeof(i));
			ImGui::TextUnformatted(label.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {

			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kGroupReorderPayloadType)) {

				const int32_t from = *static_cast<const int32_t*>(payload->Data);
				if (from != i) {
					MoveListItem(draft_.groups, from, i);
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem()) {

			if (ImGui::MenuItem("削除", nullptr, false, 1 < draft_.groups.size())) {
				removeIndex = i;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	if (0 <= removeIndex) {

		const UUID removedID = draft_.groups[removeIndex].id;
		draft_.groups.erase(draft_.groups.begin() + removeIndex);
		groupEditorStates_.erase(removedID);
		if (removedID == selectedGroupID_) {
			selectedGroupID_ = draft_.groups[(std::min)(removeIndex,
				static_cast<int32_t>(draft_.groups.size()) - 1)].id;
		}
		changed = true;
	}
	return changed;
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

bool ParticleEffectEditorTool::DrawBasicSection(
	const EditorToolContext& context, ParticleEffectGroup& group) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	bool changed = false;

	//============================================================================
	//	エミッター編集
	//============================================================================
	if (ImGui::BeginTabItem("エミッター")) {
		{
			MyGUI::ScopedPropertyLabelWidth labelWidth("EmitterGroupName");
			changed |= MyGUI::InputText("グループ名", group.name).valueChanged;
		}

		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("発生設定", false)) {
			MyGUI::ScopedPropertyLabelWidth labelWidth("EmitterEmissionSettings");

			ImGui::BeginDisabled(draft_.groupEmission.mode == ParticleEffectGroupEmissionMode::Simultaneous);
			changed |= MyGUI::DragFloat("発生間隔", group.emitter.emitInterval, MakeDragSetting(0.001f, 60.0f)).valueChanged;
			changed |= MyGUI::Checkbox("ループ再生", group.looping);
			ImGui::EndDisabled();
			changed |= ParticleGui::DrawParticleValueUInt("発生数", group.emitter.emitCount);
			{
				int32_t maxParticles = static_cast<int32_t>(group.emitter.maxParticles);
				if (MyGUI::DragInt("最大数", maxParticles).valueChanged) {

					group.emitter.maxParticles = static_cast<uint32_t>((std::max)(1, maxParticles));
					changed = true;
				}
				// 現在の発生数を集計して表示する
				uint32_t aliveCount = 0;
				if (ECSWorld* world = context.GetWorld()) {
					world->ForEach<ParticleSystemComponent>(
						[&](const Entity& entity, const ParticleSystemComponent& component) {

							const ParticleEffectInstanceRuntime* effect = ResolveEffectInstance(
								*world, entity, component, editingID_);
							if (!effect) {
								return;
							}
							for (const ParticleGroupRuntimeState& runtimeGroup :
								effect->runtimeGroups) {
								if (runtimeGroup.groupID == group.id) {
									aliveCount += static_cast<uint32_t>(
										runtimeGroup.particles.size());
								}
							}
						});
				}
				if (MyGUI::BeginPropertyRow("現在の発生数")) {

					ImGui::Text("%u / %u", aliveCount, group.emitter.maxParticles);
					MyGUI::EndPropertyRow();
				}
			}
			ImGui::Spacing();

			changed |= ParticleGui::DrawParticleValueFloat("発生初速度", group.emitter.speed, MakeDragSetting(0.0f, 10000.0f));
			changed |= ParticleGui::DrawParticleValueVector3("発生オフセット", group.emitter.emitOffset, MakeDragSetting(-10000.0f, 10000.0f));
		}
		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("エミッター形状設定", false)) {
			MyGUI::ScopedPropertyLabelWidth labelWidth("EmitterShapeSettings");

			// 空間で使える形状だけを選択候補にする
			const bool is2D = draft_.space == PrimitiveRenderSpace::Screen2D;
			ParticleEmitterShapeRegistry& shapeRegistry = ParticleEmitterShapeRegistry::GetInstance();
			const std::vector<ParticleEmitterShape> shapes = shapeRegistry.GetShapes(is2D);

			// 空間に合わない形状は先頭へフォールバック
			int32_t currentIndex = 0;
			bool found = false;
			for (int32_t i = 0; i < static_cast<int32_t>(shapes.size()); ++i) {
				if (shapes[i] == group.emitter.shape) { currentIndex = i; found = true; break; }
			}
			if (!found && !shapes.empty()) {

				group.emitter.shape = shapes.front();
				changed = true;
			}

			if (!shapes.empty() && MyGUI::BeginPropertyRow("発生形状")) {

				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
				if (ImGui::BeginCombo("##Value", EnumAdapter<ParticleEmitterShape>::ToString(shapes[currentIndex]))) {
					for (int32_t i = 0; i < static_cast<int32_t>(shapes.size()); ++i) {
						if (ImGui::Selectable(EnumAdapter<ParticleEmitterShape>::ToString(shapes[i]), i == currentIndex)) {

							group.emitter.shape = shapes[i];
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				MyGUI::EndPropertyRow();
			}

			// 形状別パラメータ
			if (const IParticleEmitterShape* shape = shapeRegistry.Find(group.emitter.shape)) {
				changed |= ParticleEditorDescriptorRegistry::GetInstance().DrawEmitterShape(
					group.emitter.shape, *shape, group.emitter);
			}
		}
		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("描画設定", false)) {
			MyGUI::ScopedPropertyLabelWidth labelWidth("ParticleRenderSettings");

			if (MyGUI::EnumCombo("描画空間", draft_.space).valueChanged) {

				for (ParticleEffectGroup& effectGroup : draft_.groups) {

					const IParticleEmitterShape* emitterShape =
						ParticleEmitterShapeRegistry::GetInstance().Find(effectGroup.emitter.shape);
					if (draft_.space == PrimitiveRenderSpace::Screen2D) {
						if (!emitterShape || !emitterShape->Supports2D()) {
							effectGroup.emitter.shape = ParticleEmitterShape::Circle;
						}
						if (effectGroup.shape != PrimitiveType::Plane && effectGroup.shape != PrimitiveType::Ring) {
							effectGroup.shape = PrimitiveType::Plane;
						}
					} else if (!emitterShape || !emitterShape->Supports3D()) {
						effectGroup.emitter.shape = ParticleEmitterShape::Sphere;
					}
				}
				changed = true;
			}
			changed |= MyGUI::EnumCombo("形状", group.shape).valueChanged;

			// 形状ごとのパラメータ
			if (const IParticlePrimitiveShapeDrawer* drawer =
				ParticlePrimitiveShapeDrawerRegistry::GetInstance().Find(group.shape)) {
				changed |= drawer->DrawImGui(group);
			}
			// 描画設定
			{
				AssetEditSetting setting{};
				changed |= MyGUI::AssetReferenceField("モデル", group.model, assetDatabase, { AssetType::Mesh }, setting).valueChanged;
			}
			{
				AssetEditSetting setting{};
				setting.defaultAssetID = draft_.space == PrimitiveRenderSpace::Screen2D ?
					BuiltinAssets::Materials::DefaultParticle2D : BuiltinAssets::Materials::DefaultParticle;
				AssetID material = group.material;
				if (MyGUI::AssetReferenceField("マテリアル", material,
					assetDatabase, { AssetType::Material }, setting).valueChanged) {

					std::string message{};
					if (ValidateParticleMaterialSelection(context, material, message)) {
						group.material = material;
						changed = true;
						statusMessage_.clear();
					} else {
						statusMessage_ = "Particleマテリアルを設定できません: " + message;
					}
				}
			}
			changed |= MyGUI::EnumCombo("ブレンドモード", group.blendMode).valueChanged;
			changed |= MyGUI::EnumCombo("キュー", group.queue).valueChanged;
			// ビルボード軸
			{
				const Axis axes[] = { Axis::X, Axis::Y, Axis::Z };
				const char* axisLabels[] = { "ビルボードX", "ビルボードY", "ビルボードZ" };
				for (int32_t i = 0; i < 3; ++i) {

					bool enabled = std::find(group.billboardAxes.begin(), group.billboardAxes.end(), axes[i]) != group.billboardAxes.end();
					if (MyGUI::Checkbox(axisLabels[i], enabled)) {

						if (enabled) {
							group.billboardAxes.emplace_back(axes[i]);
						} else {
							std::erase(group.billboardAxes, axes[i]);
						}
						changed = true;
					}
				}
			}

			ImGui::SeparatorText("トレイル");
			changed |= MyGUI::Checkbox("トレイル描画", group.trail.enabled);
			if (group.trail.enabled) {

				changed |= MyGUI::Checkbox("元の形状を描画", group.trail.drawSource);
				const bool keepAfterParticleDeath = group.trail.keepAfterParticleDeath;
				changed |= MyGUI::Checkbox("粒子消滅後もトレイルを残す", group.trail.keepAfterParticleDeath);
				if (!keepAfterParticleDeath && group.trail.keepAfterParticleDeath &&
					group.trail.pointLifetime <= 0.0f) {

					group.trail.pointLifetime = ParticleTrailSettings::kDefaultPointLifetime;
					changed = true;
				}
				if (group.trail.keepAfterParticleDeath) {
					changed |= MyGUI::Checkbox("寿命後も更新", group.trail.continueUpdateAfterParticleDeath);
				}
				changed |= MyGUI::DragInt("軌跡点の上限", group.trail.maxPoints).valueChanged;
				changed |= MyGUI::DragFloat("最小移動距離", group.trail.minDistance, MakeDragSetting(0.001f, 100.0f)).valueChanged;
				changed |= MyGUI::DragFloat("点の寿命", group.trail.pointLifetime, MakeDragSetting(0.0f, 60.0f)).valueChanged;
				{
					// 未設定なら粒子と同じマテリアルを使う
					AssetEditSetting setting{};
					AssetID material = group.trail.material;
					if (MyGUI::AssetReferenceField("トレイルマテリアル", material,
						assetDatabase, { AssetType::Material }, setting).valueChanged) {

						std::string message{};
						if (ValidateParticleMaterialSelection(context, material, message)) {
							group.trail.material = material;
							changed = true;
							statusMessage_.clear();
						} else {
							statusMessage_ = "トレイルマテリアルを設定できません: " + message;
						}
					}
				}
				changed |= DrawTrailMaterialSection(context, group);
			}
		}
		ImGui::EndTabItem();
	}
	return changed;
}

bool ParticleEffectEditorTool::DrawPhaseSection(const EditorToolContext& context,
	ParticleEffectGroup& group, GroupEditorState& editorState) {

	bool changed = false;
	int32_t& selectedPhase = editorState.selectedPhase;
	auto& moduleCache = editorState.moduleCache;
	auto& selectedModules = editorState.selectedModules;

	//============================================================================
	//	フェーズ編集
	//============================================================================
	if (ImGui::BeginTabItem("フェーズ")) {

		// フェーズは必ず1つ以上持つ
		if (group.phases.empty()) {

			group.phases.emplace_back();
			changed = true;
		}
		// 編集用インスタンスの数をフェーズ数へ合わせる
		if (moduleCache.size() != group.phases.size()) {
			moduleCache.resize(group.phases.size());
		}
		if (selectedModules.size() != group.phases.size()) {
			selectedModules.resize(group.phases.size(), 0);
		}
		selectedPhase = std::clamp(selectedPhase, 0, static_cast<int32_t>(group.phases.size()) - 1);

		//========================================================================================================================================================
		// 左のフェーズリスト、選択と追加と削除と並べ替え
		ImGui::BeginChild("PhaseList", ImVec2(90.0f, 0.0f), true);
		if (ImGui::Button("追加", ImVec2(-FLT_MIN, 0.0f))) {

			ParticleEffectPhase phase{};
			phase.name = "Phase " + std::to_string(group.phases.size() + 1);
			group.phases.emplace_back(std::move(phase));
			moduleCache.emplace_back();
			selectedModules.emplace_back(0);
			selectedPhase = static_cast<int32_t>(group.phases.size()) - 1;
			changed = true;
		}
		ImGui::Separator();
		int32_t removePhaseIndex = -1;
		for (int32_t i = 0; i < static_cast<int32_t>(group.phases.size()); ++i) {

			ImGui::PushID(i);
			const std::string label = std::to_string(i + 1) + ": " + group.phases[i].name;
			if (ImGui::Selectable(label.c_str(), i == selectedPhase)) {
				selectedPhase = i;
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

						MoveListItem(group.phases, from, i);
						MoveListItem(moduleCache, from, i);
						MoveListItem(selectedModules, from, i);
						// 選択中のフェーズを追従させる
						if (from == selectedPhase) {
							selectedPhase = i;
						} else if (from < selectedPhase && selectedPhase <= i) {
							--selectedPhase;
						} else if (i <= selectedPhase && selectedPhase < from) {
							++selectedPhase;
						}
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			// 右クリックで削除、最後の1つは消せない
			if (ImGui::BeginPopupContextItem()) {

				if (ImGui::MenuItem("削除", nullptr, false, 1 < group.phases.size())) {
					removePhaseIndex = i;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		if (0 <= removePhaseIndex) {

			group.phases.erase(group.phases.begin() + removePhaseIndex);
			moduleCache.erase(moduleCache.begin() + removePhaseIndex);
			selectedModules.erase(selectedModules.begin() + removePhaseIndex);
			selectedPhase = std::clamp(selectedPhase, 0, static_cast<int32_t>(group.phases.size()) - 1);
			changed = true;
		}
		ImGui::EndChild();

		//========================================================================================================================================================
		// 右の選択フェーズ編集
		ImGui::SameLine();
		ImGui::BeginChild("PhaseEdit", ImVec2(0.0f, 0.0f));

		ParticleEffectPhase& phase = group.phases[selectedPhase];
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

		changed |= DrawPhaseMaterialSection(context, group, phase);
		changed |= DrawPhaseParentSection(context, group, phase, selectedPhase);

		ImGui::SeparatorText("モジュール");
		changed |= DrawPhaseModules(context, group, editorState, phase);
		ImGui::EndChild();

		ImGui::EndTabItem();
	}
	return changed;
}

bool ParticleEffectEditorTool::DrawPhaseMaterialSection(const EditorToolContext& context,
	ParticleEffectGroup& group, ParticleEffectPhase& phase) {

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

	const AssetID materialID = ResolvePhaseMaterialID(draft_, group, phase);
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

bool ParticleEffectEditorTool::DrawTrailMaterialSection(
	const EditorToolContext& context, ParticleEffectGroup& group) {

	if (!MyGUI::CollapsingHeader("トレイルテクスチャ設定", false)) {
		return false;
	}
	MyGUI::ScopedPropertyLabelWidth labelWidth("TrailTextureSettings");

	bool changed = false;
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	ParticlePhaseMaterialSettings& materialSettings = group.trail.materialSettings;
	{
		AssetEditSetting setting{};
		changed |= MyGUI::AssetReferenceField("ベースカラーテクスチャ", materialSettings.baseColorTexture,
			assetDatabase, { AssetType::Texture }, setting).valueChanged;
	}

	const std::optional<MaterialAsset> material = LoadMaterialAsset(
		context, ResolveTrailMaterialID(draft_, group));
	if (!material) {
		ImGui::TextDisabled("マテリアルを解決できません");
		return changed;
	}
	const ShaderReflectionInfo* reflection = FindParticleMaterialReflection(context, *material);
	if (!reflection) {
		ImGui::TextDisabled("シェーダーリフレクションを取得できません");
		return changed;
	}

	for (const ShaderResourceBinding& texture : CollectMaterialTextures(*reflection)) {

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
	return changed;
}

bool ParticleEffectEditorTool::DrawPhaseParentSection(const EditorToolContext& context,
	ParticleEffectGroup& group, ParticleEffectPhase& phase, int32_t selectedPhase) {

	bool changed = false;
	ParticlePhaseParentSettings& settings = phase.parentSettings;
	if (!MyGUI::CollapsingHeader("ペアレント設定", false)) {
		return false;
	}

	if (MyGUI::Checkbox("エミッターを親にする", settings.useEmitter)) {

		if (settings.useEmitter) {
			settings.entityLocalFileID = {};
		}
		changed = true;
	}

	Engine::UUID parent = settings.entityLocalFileID;
	if (DrawParticleParentField("親エンティティ", context.GetWorld(), parent).valueChanged) {

		settings.entityLocalFileID = parent;
		if (parent) {
			settings.useEmitter = false;
		}
		changed = true;
	}
	changed |= MyGUI::Checkbox("親の回転を無視", settings.ignoreParentRotation);
	changed |= MyGUI::Checkbox("親のスケールを無視", settings.ignoreParentScale);

	const bool previousHasParent = 0 < selectedPhase &&
		group.phases[selectedPhase - 1].parentSettings.HasParent();
	if (previousHasParent && !settings.HasParent()) {
		changed |= MyGUI::Checkbox("ワールドを保持", settings.keepWorldOnDetach);
	}

	return changed;
}

bool ParticleEffectEditorTool::DrawPhaseModules(const EditorToolContext& context,
	ParticleEffectGroup& group, GroupEditorState& editorState, ParticleEffectPhase& phase) {

	bool changed = false;
	std::vector<ModuleCacheEntry>& cache = editorState.moduleCache[editorState.selectedPhase];
	if (cache.size() != phase.modules.size()) {
		cache.resize(phase.modules.size());
	}
	int32_t& selectedModule = editorState.selectedModules[editorState.selectedPhase];
	selectedModule = phase.modules.empty() ? -1 :
		std::clamp(selectedModule, 0, static_cast<int32_t>(phase.modules.size()) - 1);

	ImGui::BeginChild("ModuleList", ImVec2(190.0f, 0.0f), true);
	const auto& moduleDescriptors = ParticleModuleRegistry::GetInstance().GetDescriptors();
	if (ImGui::Button("モジュール追加", ImVec2(-FLT_MIN, 0.0f))) {
		ImGui::OpenPopup("##AddParticleModulePopup");
	}
	if (ImGui::BeginPopup("##AddParticleModulePopup")) {

		ImTextureID searchIcon{};
		if (context.panelContext && context.panelContext->graphicsCore) {
			searchIcon = EditorTextureHelper::GetSearchIcon(
				context.panelContext->graphicsCore->GetTextureUploadService());
		}
		addModuleSearchFilter_.DrawInput("##AddParticleModuleSearch", searchIcon, "検索...");
		ImGui::Separator();

		bool hasAny = false;
		for (const ParticleModuleRegistry::Descriptor& descriptor : moduleDescriptors) {

			if (!addModuleSearchFilter_.Matches(descriptor.id)) {
				continue;
			}
			hasAny = true;
			if (ImGui::MenuItem(descriptor.id.c_str())) {

				ParticleEffectModuleEntry entry{};
				entry.id = descriptor.id;
				if (auto module = ParticleModuleRegistry::GetInstance().Create(descriptor.typeID)) {
					entry.params = module->ToJson();
				}
				phase.modules.emplace_back(std::move(entry));
				cache.emplace_back();
				selectedModule = static_cast<int32_t>(phase.modules.size()) - 1;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
		}
		if (!hasAny) {
			ImGui::TextDisabled("追加できるモジュールがありません");
		}
		ImGui::EndPopup();
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
		ImGui::TextUnformatted(moduleName.c_str());
		ImGui::Separator();
		// キャッシュされたモジュールリストの中から選択IDで引く
		if (IParticleModule* module = ResolveModuleCache(cache[selectedModule], entry)) {
			MyGUI::ScopedPropertyLabelWidth labelWidth(entry.id.c_str());
			if (auto* custom = dynamic_cast<ParticleCustomShaderParameterModule*>(module)) {

				std::vector<ShaderConstantBufferVariable> parameters{};
				const AssetID materialID = entry.id == "TrailCustomShaderParameter" ?
					ResolveTrailMaterialID(draft_, group) : ResolvePhaseMaterialID(draft_, group, phase);
				if (const std::optional<MaterialAsset> material = LoadMaterialAsset(context, materialID)) {
					if (const ShaderReflectionInfo* reflection = FindParticleMaterialReflection(context, *material)) {
						parameters = CollectMaterialParameters(*reflection);
					}
				}
				custom->SetReflectedParameters(parameters);
			}
			if (ParticleEditorDescriptorRegistry::GetInstance().DrawModule(
				cache[selectedModule].typeID, *module)) {

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

ParticleEffectGroup* ParticleEffectEditorTool::GetSelectedGroup() {

	auto it = std::find_if(draft_.groups.begin(), draft_.groups.end(), [&](const ParticleEffectGroup& group) {
		return group.id == selectedGroupID_;
		});
	if (it != draft_.groups.end()) { return &*it; }
	if (draft_.groups.empty()) { return nullptr; }
	selectedGroupID_ = draft_.groups.front().id;
	return &draft_.groups.front();
}

ParticleEffectEditorTool::GroupEditorState& ParticleEffectEditorTool::GetGroupEditorState(UUID groupID) {

	return groupEditorStates_[groupID];
}

void ParticleEffectEditorTool::RestartParticleSystems(
	const EditorToolContext& context, bool oneShot) {

	ECSWorld* world = context.GetWorld();
	if (!world) {

		statusMessage_ = "再生対象のシーンがありません";
		return;
	}
	// 対象エフェクトを使っているParticleSystemを頭から再生する
	size_t restartCount = 0;
	world->ForEach<ParticleSystemComponent>(
		[&](const Entity& entity, const ParticleSystemComponent& component) {

		if (!UsesEffect(component, editingID_)) { return; }
		RequestParticleSystemRestart(*world, entity, oneShot);
		++restartCount;
		});
	statusMessage_ = 0 < restartCount ?
		std::string{} : "編集中のエフェクトを使用するParticleSystemがありません";
}

void ParticleEffectEditorTool::StopParticleSystems(
	const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world) {

		statusMessage_ = "停止対象のシーンがありません";
		return;
	}
	// 対象エフェクトを使っているParticleSystemを停止して粒子を消す
	size_t stopCount = 0;
	world->ForEach<ParticleSystemComponent>(
		[&](const Entity& entity, const ParticleSystemComponent& component) {

		if (!UsesEffect(component, editingID_)) { return; }
		RequestParticleSystemStop(*world, entity,
			ParticleSystemStopBehavior::StopEmittingAndClear);
		++stopCount;
		});
	statusMessage_ = 0 < stopCount ?
		std::string{} : "編集中のエフェクトを使用するParticleSystemがありません";
}

Engine::IParticleModule* ParticleEffectEditorTool::ResolveModuleCache(ModuleCacheEntry& cache, const ParticleEffectModuleEntry& entry) {

	// idが変わっていたら作り直し、現在のパラメータを読み込ませる
	if (!cache.module || cache.id != entry.id) {

		cache.id = entry.id;
		ParticleModuleRegistry& registry = ParticleModuleRegistry::GetInstance();
		cache.typeID = registry.FindTypeID(entry.id);
		cache.module = registry.Create(cache.typeID);
		if (cache.module) {
			cache.module->FromJson(entry.params);
		}
	}
	return cache.module.get();
}

void ParticleEffectEditorTool::LoadEffect(const EditorToolContext& context, AssetID effectID) {

	loaded_ = false;
	editingID_ = effectID;
	groupEditorStates_.clear();
	selectedGroupID_ = {};
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
	selectedGroupID_ = draft_.groups.front().id;
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
	ParticleEffectGroup group{};
	group.name = "Group 1";
	ParticleEffectPhase phase{};
	phase.name = "Phase 1";
	phase.modules = {
		{ "SizeOverLifetime", nlohmann::json::object() },
		{ "ColorOverLifetime", nlohmann::json::object() },
	};
	group.phases.emplace_back(std::move(phase));
	asset.groups.emplace_back(std::move(group));

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
