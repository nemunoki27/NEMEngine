#include "ParticleEffectGroupDrawer.h"

//============================================================================
//	include
//============================================================================
#include "ParticleEffectMaterialResolver.h"
#include "ParticleEffectTextureDrawer.h"
#include "GUI/ParticleGUIHelpers.h"
#include "ParticleEditorDescriptorRegistry.h"
#include "ParticleEffectPreviewOperations.h"
#include "ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

using namespace Engine;
using namespace Engine::ParticleEffectMaterialResolver;
using namespace Engine::ParticleEffectPreviewOperations;
using Engine::ParticleGUI::MakeDragSetting;

ParticleEffectGroupDrawer::ParticleEffectGroupDrawer(ParticleEffectEditSession& session, std::string& status) :
	session_(session), statusMessage_(status) {}

bool ParticleEffectGroupDrawer::Draw(const EditorToolContext& context, ParticleEffectGroup& group) {

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

			ImGui::BeginDisabled(session_.GetDraft().groupEmission.mode == ParticleEffectGroupEmissionMode::Simultaneous);
			changed |= MyGUI::DragFloat("発生間隔", group.emitter.emitInterval, MakeDragSetting(0.001f, 60.0f)).valueChanged;
			changed |= MyGUI::Checkbox("ループ再生", group.looping);
			ImGui::EndDisabled();
			changed |= ParticleGUI::DrawParticleValueUInt("発生数", group.emitter.emitCount);
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
								*world, entity, component, session_.GetEditingID());
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

			changed |= ParticleGUI::DrawParticleValueFloat("発生初速度", group.emitter.speed, MakeDragSetting(0.0f, 10000.0f));
			changed |= ParticleGUI::DrawParticleValueVector3("発生オフセット", group.emitter.emitOffset, MakeDragSetting(-10000.0f, 10000.0f));
		}
		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("エミッター形状設定", false)) {
			MyGUI::ScopedPropertyLabelWidth labelWidth("EmitterShapeSettings");

			// 空間で使える形状だけを選択候補にする
			const bool is2D = session_.GetDraft().space == PrimitiveRenderSpace::Screen2D;
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

			if (MyGUI::EnumCombo("描画空間", session_.GetDraft().space).valueChanged) {

				for (ParticleEffectGroup& effectGroup : session_.GetDraft().groups) {

					const IParticleEmitterShape* emitterShape =
						ParticleEmitterShapeRegistry::GetInstance().Find(effectGroup.emitter.shape);
					if (session_.GetDraft().space == PrimitiveRenderSpace::Screen2D) {
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
				setting.defaultAssetID = session_.GetDraft().space == PrimitiveRenderSpace::Screen2D ?
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
			changed |= InspectorDrawerCommon::DrawLayerMaskField(*context.panelContext,
				"Rendering Layer", group.renderingLayerMask).valueChanged;
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

bool ParticleEffectGroupDrawer::DrawTrailMaterialSection(const EditorToolContext& context, ParticleEffectGroup& group) {

	if (!MyGUI::CollapsingHeader("トレイルテクスチャ設定", false)) { return false; }
	MyGUI::ScopedPropertyLabelWidth labelWidth("TrailTextureSettings");
	return ParticleEffectTextureDrawer::Draw(context, group.trail.materialSettings, ResolveTrailMaterialID(session_.GetDraft(), group));
}