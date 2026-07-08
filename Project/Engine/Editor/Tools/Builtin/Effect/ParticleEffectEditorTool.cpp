#include "ParticleEffectEditorTool.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>
#include <Engine/Core/Rendering/Particle/ParticleModuleRegistry.h>
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <algorithm>
#include <filesystem>
#include <numbers>

//============================================================================
//	ParticleEffectEditorTool internal
//============================================================================
namespace {

	// float編集の共通設定
	FloatEditSetting MakeDragSetting(float minValue, float maxValue, float dragSpeed = 0.01f) {

		FloatEditSetting setting{};
		setting.dragSpeed = dragSpeed;
		setting.minValue = minValue;
		setting.maxValue = maxValue;
		return setting;
	}

	// 定数かランダムかを切り替えられるfloat値を編集する
	bool DrawParticleValueFloat(const char* label, ParticleValue<float>& value,
		const FloatEditSetting& setting) {

		bool changed = false;
		ImGui::PushID(label);

		changed |= MyGUI::EnumCombo<ParticleValueType>("タイプ", value.type).valueChanged;

		if (value.type == ParticleValueType::Constant) {

			changed |= MyGUI::DragFloat(label, value.constant, setting).valueChanged;
		} else {

			changed |= MyGUI::DragFloat((std::string(label) + " 最小").c_str(), value.min, setting).valueChanged;
			changed |= MyGUI::DragFloat((std::string(label) + " 最大").c_str(), value.max, setting).valueChanged;
		}
		ImGui::PopID();
		return changed;
	}

	// 定数かランダムかを切り替えられるuint値を編集する
	bool DrawParticleValueUInt(const char* label, ParticleValue<uint32_t>& value) {

		bool changed = false;
		ImGui::PushID(label);

		changed |= MyGUI::EnumCombo<ParticleValueType>("タイプ", value.type).valueChanged;

		auto dragUInt = [&](const char* dragLabel, uint32_t& target) {
			int32_t intValue = static_cast<int32_t>(target);
			if (MyGUI::DragInt(dragLabel, intValue).valueChanged) {

				target = static_cast<uint32_t>((std::max)(0, intValue));
				return true;
			}
			return false;
			};
		if (value.type == ParticleValueType::Constant) {
			changed |= dragUInt(label, value.constant);
		} else {

			changed |= dragUInt((std::string(label) + " 最小").c_str(), value.min);
			changed |= dragUInt((std::string(label) + " 最大").c_str(), value.max);
		}
		ImGui::PopID();
		return changed;
	}

	// jsonのfloat値を編集する
	bool DragJsonFloat(const char* label, nlohmann::json& params, const char* key,
		float defaultValue, const FloatEditSetting& setting) {

		float value = params.value(key, defaultValue);
		if (MyGUI::DragFloat(label, value, setting).valueChanged) {

			params[key] = value;
			return true;
		}
		return false;
	}

	// jsonのint値を編集する
	bool DragJsonInt(const char* label, nlohmann::json& params, const char* key, int32_t defaultValue) {

		int32_t value = params.value(key, defaultValue);
		if (MyGUI::DragInt(label, value).valueChanged) {

			params[key] = value;
			return true;
		}
		return false;
	}

	// jsonの色値を編集する
	bool ColorEditJson(const char* label, nlohmann::json& params, const char* key, const Color4& defaultValue) {

		Color4 value = defaultValue;
		if (const auto it = params.find(key); it != params.end()) {
			value = Color4::FromJson(*it);
		}
		if (MyGUI::ColorEdit(label, value).valueChanged) {

			params[key] = value.ToJson();
			return true;
		}
		return false;
	}

	// jsonのイージングを編集する
	bool EasingComboJson(nlohmann::json& params, const char* key) {

		EasingType easing = EnumAdapter<EasingType>::FromString(
			params.value(key, "EaseOutSine")).value_or(EasingType::EaseOutSine);
		const EasingType previous = easing;
		Easing::SelectEasingType(easing, "easing");
		if (easing != previous) {

			params[key] = EnumAdapter<EasingType>::ToString(easing);
			return true;
		}
		return false;
	}

	// jsonに入っているRing形状パラメータを編集する
	bool DrawRingParamsJson(const char* label, nlohmann::json& params, const char* key) {

		if (!params.contains(key) || !params[key].is_object()) {
			params[key] = PrimitiveRingParams{};
		}
		nlohmann::json& obj = params[key];
		bool changed = false;
		ImGui::SeparatorText(label);
		ImGui::PushID(label);
		changed |= DragJsonFloat("外周半径", obj, "outerRadius", 1.0f, MakeDragSetting(0.0f, 10000.0f));
		changed |= DragJsonFloat("内周半径", obj, "innerRadius", 0.5f, MakeDragSetting(0.0f, 10000.0f));
		changed |= DragJsonFloat("開始角", obj, "startAngle", 0.0f, MakeDragSetting(0.0f, 360.0f, 0.5f));
		changed |= DragJsonFloat("終了角", obj, "endAngle", 360.0f, MakeDragSetting(0.0f, 360.0f, 0.5f));
		ImGui::PopID();
		return changed;
	}

	// jsonに入っているCylinder形状パラメータを編集する
	bool DrawCylinderParamsJson(const char* label, nlohmann::json& params, const char* key) {

		if (!params.contains(key) || !params[key].is_object()) {
			params[key] = PrimitiveCylinderParams{};
		}
		nlohmann::json& obj = params[key];
		bool changed = false;
		ImGui::SeparatorText(label);
		ImGui::PushID(label);
		changed |= DragJsonFloat("上面半径", obj, "topRadius", 1.0f, MakeDragSetting(0.0f, 10000.0f));
		changed |= DragJsonFloat("下面半径", obj, "bottomRadius", 1.0f, MakeDragSetting(0.0f, 10000.0f));
		changed |= DragJsonFloat("高さ", obj, "height", 2.0f, MakeDragSetting(0.0f, 10000.0f));
		changed |= DragJsonFloat("展開角", obj, "maxAngle", std::numbers::pi_v<float> *2.0f, MakeDragSetting(0.0f, 10.0f));
		ImGui::PopID();
		return changed;
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
			changed |= DrawModuleSection();

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
			changed |= DrawParticleValueUInt("発生数", draft_.emitter.emitCount);
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
			ImGui::Separator();

			changed |= MyGUI::DragFloat("エミッター再生時間", draft_.duration, MakeDragSetting(0.01f, 600.0f)).valueChanged;
			changed |= MyGUI::Checkbox("ループ再生", draft_.looping);

			changed |= DrawParticleValueFloat("寿命", draft_.emitter.lifetime, MakeDragSetting(0.001f, 600.0f));
			changed |= DrawParticleValueFloat("発生初速度", draft_.emitter.speed, MakeDragSetting(0.0f, 10000.0f));
		}
		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("形状設定", false)) {

			// 2D描画かどうか
			bool is2D = draft_.space == PrimitiveRenderSpace::Screen2D;
			// 3D形状
			ParticleEmitterShape shapes3D[] = {
			   ParticleEmitterShape::Sphere, ParticleEmitterShape::Hemisphere, ParticleEmitterShape::Box,
			   ParticleEmitterShape::Torus, ParticleEmitterShape::Circle, ParticleEmitterShape::Cone, ParticleEmitterShape::Point };
			// 2D形状
			ParticleEmitterShape shapes2D[] = {
			   ParticleEmitterShape::Circle, ParticleEmitterShape::Rect,
			   ParticleEmitterShape::Point, ParticleEmitterShape::Cone2D };
			ParticleEmitterShape* shapes = is2D ? shapes2D : shapes3D;
			// 形状数
			int32_t shapeCount = is2D ? sizeof(shapes2D) : sizeof(shapes3D);

			// 空間に合わない形状は先頭へフォールバック
			int32_t currentIndex = 0;
			bool found = false;
			for (int32_t i = 0; i < shapeCount; ++i) {
				if (shapes[i] == draft_.emitter.shape) { currentIndex = i; found = true; break; }
			}
			if (!found) {

				draft_.emitter.shape = shapes[0];
				changed = true;
			}

			if (ImGui::BeginCombo("発生形状", EnumAdapter<ParticleEmitterShape>::ToString(shapes[currentIndex]))) {
				for (int32_t i = 0; i < shapeCount; ++i) {
					if (ImGui::Selectable(EnumAdapter<ParticleEmitterShape>::ToString(shapes[i]), i == currentIndex)) {

						draft_.emitter.shape = shapes[i];
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			// 形状別パラメータ
			switch (draft_.emitter.shape) {
				//============================================================================
				//	球
				//============================================================================
			case ParticleEmitterShape::Sphere:
			case ParticleEmitterShape::Hemisphere:
				changed |= MyGUI::DragFloat("半径", draft_.emitter.sphereRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				break;
				//============================================================================
				//	ボックス
				//============================================================================
			case ParticleEmitterShape::Box:
				changed |= MyGUI::DragVector3("大きさ", draft_.emitter.boxSize, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::Checkbox("+X面", draft_.emitter.boxFacePosX);
				ImGui::SameLine();
				changed |= MyGUI::Checkbox("-X面", draft_.emitter.boxFaceNegX);
				changed |= MyGUI::Checkbox("+Y面", draft_.emitter.boxFacePosY);
				ImGui::SameLine();
				changed |= MyGUI::Checkbox("-Y面", draft_.emitter.boxFaceNegY);
				changed |= MyGUI::Checkbox("+Z面", draft_.emitter.boxFacePosZ);
				ImGui::SameLine();
				changed |= MyGUI::Checkbox("-Z面", draft_.emitter.boxFaceNegZ);
				break;
				//============================================================================
				//	トーラス
				//============================================================================
			case ParticleEmitterShape::Torus:
				changed |= MyGUI::DragFloat("主半径", draft_.emitter.torusRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragFloat("管半径", draft_.emitter.torusThickness, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				break;
				//============================================================================
				//	円
				//============================================================================
			case ParticleEmitterShape::Circle:
				changed |= MyGUI::DragFloat("半径", draft_.emitter.circleRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragFloat("円弧角度", draft_.emitter.circleArc, MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
				break;
				//============================================================================
				//	3Dコーン型
				//============================================================================
			case ParticleEmitterShape::Cone:
				changed |= MyGUI::DragFloat("開き角", draft_.emitter.coneAngle, MakeDragSetting(0.0f, 89.0f, 0.5f)).valueChanged;
				changed |= MyGUI::DragFloat("底面半径", draft_.emitter.coneRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				break;
				//============================================================================
				//	点
				//============================================================================
			case ParticleEmitterShape::Point:
				changed |= MyGUI::DragVector3("射出方向", draft_.emitter.pointDirection, MakeDragSetting(-1.0f, 1.0f)).valueChanged;
				break;
				//============================================================================
				//	矩形
				//============================================================================
			case ParticleEmitterShape::Rect:
				changed |= MyGUI::DragVector2("大きさ", draft_.emitter.rectSize, MakeDragSetting(0.0f, 100000.0f)).valueChanged;
				changed |= MyGUI::Checkbox("+X辺", draft_.emitter.rectEdgePosX);
				ImGui::SameLine();
				changed |= MyGUI::Checkbox("-X辺", draft_.emitter.rectEdgeNegX);
				changed |= MyGUI::Checkbox("+Y辺", draft_.emitter.rectEdgePosY);
				ImGui::SameLine();
				changed |= MyGUI::Checkbox("-Y辺", draft_.emitter.rectEdgeNegY);
				break;
				//============================================================================
				//	2Dコーン型
				//============================================================================
			case ParticleEmitterShape::Cone2D:
				changed |= MyGUI::DragFloat("開き角", draft_.emitter.coneAngle, MakeDragSetting(0.0f, 89.0f, 0.5f)).valueChanged;
				changed |= MyGUI::DragFloat("底辺半径", draft_.emitter.coneRadius, MakeDragSetting(0.0f, 100000.0f)).valueChanged;
				break;
			}
		}
		//========================================================================================================================================================
		if (MyGUI::CollapsingHeader("描画設定", false)) {

			changed |= MyGUI::EnumCombo("描画空間", draft_.space).valueChanged;
			changed |= MyGUI::EnumCombo("形状", draft_.shape).valueChanged;

			// 形状ごとのパラメータ
			switch (draft_.shape) {
			case PrimitiveType::Plane:
				changed |= MyGUI::DragVector2("大きさ", draft_.plane.size, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				break;
			case PrimitiveType::CrossPlane:
				changed |= MyGUI::DragVector2("大きさ", draft_.crossPlane.size, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragInt("枚数", draft_.crossPlane.planeCount).valueChanged;
				break;
			case PrimitiveType::Ring:
				changed |= MyGUI::DragFloat("外周半径", draft_.ring.outerRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragFloat("内周半径", draft_.ring.innerRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragFloat("開始角", draft_.ring.startAngle, MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
				changed |= MyGUI::DragFloat("終了角", draft_.ring.endAngle, MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
				changed |= MyGUI::DragInt("分割数", draft_.ring.divide).valueChanged;
				break;
			case PrimitiveType::Cylinder:
				changed |= MyGUI::DragFloat("上面半径", draft_.cylinder.topRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragFloat("下面半径", draft_.cylinder.bottomRadius, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragFloat("高さ", draft_.cylinder.height, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				changed |= MyGUI::DragFloat("展開角", draft_.cylinder.maxAngle, MakeDragSetting(0.0f, 10.0f)).valueChanged;
				changed |= MyGUI::DragInt("円周分割", draft_.cylinder.radialDivide).valueChanged;
				break;
			case PrimitiveType::Sphere:
				changed |= MyGUI::DragFloat("半径", draft_.sphere.radius, MakeDragSetting(0.001f, 10000.0f)).valueChanged;
				break;
			case PrimitiveType::Hemisphere:
				changed |= MyGUI::DragFloat("半径", draft_.hemisphere.radius, MakeDragSetting(0.001f, 10000.0f)).valueChanged;
				break;
			case PrimitiveType::Cube:
				changed |= MyGUI::DragVector3("大きさ", draft_.cube.size, MakeDragSetting(0.0f, 10000.0f)).valueChanged;
				break;
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
				changed |= MyGUI::AssetReferenceField("マテリアル", draft_.material, assetDatabase, { AssetType::Material }, setting).valueChanged;
			}
			changed |= MyGUI::EnumCombo("ソート", draft_.sortMode).valueChanged;
			// ビルボード軸、BillboardComponentと同じ軸マスク方式
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
				changed |= MyGUI::DragFloat("リボン幅", draft_.trail.width, MakeDragSetting(0.001f, 100.0f)).valueChanged;
			}
		}
		ImGui::EndTabItem();
	}
	return changed;
}

bool ParticleEffectEditorTool::DrawModuleSection() {

	bool changed = false;
	ImGui::SeparatorText("モジュール");

	// モジュールの追加
	const std::vector<std::string> registeredIDs = ParticleModuleRegistry::GetInstance().GetRegisteredIDs();
	if (!registeredIDs.empty()) {

		addModuleIndex_ = std::clamp(addModuleIndex_, 0, static_cast<int32_t>(registeredIDs.size()) - 1);
		if (ImGui::BeginCombo("##AddModule", registeredIDs[addModuleIndex_].c_str())) {
			for (int32_t i = 0; i < static_cast<int32_t>(registeredIDs.size()); ++i) {
				if (ImGui::Selectable(registeredIDs[i].c_str(), i == addModuleIndex_)) {
					addModuleIndex_ = i;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("追加")) {

			ParticleEffectModuleEntry entry{};
			entry.id = registeredIDs[addModuleIndex_];
			draft_.modules.emplace_back(std::move(entry));
			changed = true;
		}
	}

	// モジュール一覧、削除と並べ替えとパラメータ編集
	int32_t removeIndex = -1;
	for (int32_t i = 0; i < static_cast<int32_t>(draft_.modules.size()); ++i) {

		ParticleEffectModuleEntry& entry = draft_.modules[i];
		ImGui::PushID(i);
		const bool open = ImGui::CollapsingHeader(entry.id.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		if (open) {

			if (ImGui::SmallButton("削除")) {
				removeIndex = i;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("上へ") && 0 < i) {

				std::swap(draft_.modules[i], draft_.modules[i - 1]);
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("下へ") && i + 1 < static_cast<int32_t>(draft_.modules.size())) {

				std::swap(draft_.modules[i], draft_.modules[i + 1]);
				changed = true;
			}
			changed |= DrawModuleParams(entry.id, entry.params, i);
		}
		ImGui::PopID();
	}
	if (0 <= removeIndex) {

		draft_.modules.erase(draft_.modules.begin() + removeIndex);
		changed = true;
	}
	return changed;
}

bool ParticleEffectEditorTool::DrawModuleParams(const std::string& id, nlohmann::json& params, int32_t moduleIndex) {

	bool changed = false;
	if (id == "SizeOverLifetime") {

		changed |= DragJsonFloat("開始倍率", params, "startScale", 1.0f, MakeDragSetting(0.0f, 100.0f));
		changed |= DragJsonFloat("終了倍率", params, "endScale", 0.0f, MakeDragSetting(0.0f, 100.0f));
		changed |= EasingComboJson(params, "easingType");
		bool useCurve = params.value("useCurve", false);
		if (MyGUI::Checkbox("カーブを使用", useCurve)) {

			params["useCurve"] = useCurve;
			changed = true;
		}
		if (useCurve) {

			CurveFloat curve{};
			if (const auto it = params.find("curve"); it != params.end() && it->is_object()) {
				from_json(*it, curve.channel);
			}
			CurveEditSetting setting{};
			setting.size = ImVec2(0.0f, 200.0f);
			setting.showSidePanels = false;
			if (MyGUI::CurveEditor("SizeCurve", curve, curveStates_[moduleIndex], setting).valueChanged) {

				nlohmann::json channelJson;
				to_json(channelJson, curve.channel);
				params["curve"] = std::move(channelJson);
				changed = true;
			}
		}
	} else if (id == "ColorOverLifetime") {

		changed |= ColorEditJson("開始色", params, "startColor", Color4::White());
		changed |= ColorEditJson("終了色", params, "endColor", Color4(1.0f, 1.0f, 1.0f, 0.0f));
		changed |= EasingComboJson(params, "easingType");
		bool useCurve = params.value("useCurve", false);
		if (MyGUI::Checkbox("カーブを使用", useCurve)) {

			params["useCurve"] = useCurve;
			changed = true;
		}
		if (useCurve) {

			CurveColor4 curve{};
			if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

				const size_t count = (std::min)(curve.channels.size(), it->size());
				for (size_t c = 0; c < count; ++c) {
					from_json((*it)[c], curve.channels[c]);
				}
			}
			CurveEditSetting setting{};
			setting.size = ImVec2(0.0f, 200.0f);
			setting.showSidePanels = false;
			if (MyGUI::CurveEditor("ColorCurve", curve, curveStates_[moduleIndex], setting).valueChanged) {

				nlohmann::json channels = nlohmann::json::array();
				for (const CurveChannel& channel : curve.channels) {
					channels.push_back(channel);
				}
				params["curveChannels"] = std::move(channels);
				changed = true;
			}
		}
	} else if (id == "RotationOverLifetime") {

		changed |= DragJsonFloat("初期回転最小", params, "initialMin", 0.0f, MakeDragSetting(-360.0f, 360.0f, 0.5f));
		changed |= DragJsonFloat("初期回転最大", params, "initialMax", 360.0f, MakeDragSetting(-360.0f, 360.0f, 0.5f));
		changed |= DragJsonFloat("回転速度最小", params, "speedMin", -90.0f, MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
		changed |= DragJsonFloat("回転速度最大", params, "speedMax", 90.0f, MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
	} else if (id == "GravityForce") {

		Vector3 gravity = Vector3(0.0f, -9.8f, 0.0f);
		if (const auto it = params.find("gravity"); it != params.end()) {
			gravity = Vector3::FromJson(*it);
		}
		if (MyGUI::DragVector3("重力", gravity, MakeDragSetting(-1000.0f, 1000.0f)).valueChanged) {

			params["gravity"] = gravity.ToJson();
			changed = true;
		}
	} else if (id == "NoiseForce") {

		changed |= DragJsonFloat("強さ", params, "strength", 1.0f, MakeDragSetting(0.0f, 1000.0f));
		changed |= DragJsonFloat("周波数", params, "frequency", 1.0f, MakeDragSetting(0.001f, 100.0f));
	} else if (id == "Flipbook") {

		changed |= DragJsonInt("分割X", params, "tilesX", 1);
		changed |= DragJsonInt("分割Y", params, "tilesY", 1);
		changed |= DragJsonFloat("周回数", params, "cycles", 1.0f, MakeDragSetting(0.01f, 100.0f));
	} else if (id == "ShapeOverLifetime") {

		// 対象形状の選択
		PrimitiveType shape = EnumAdapter<PrimitiveType>::FromString(
			params.value("shape", "Ring")).value_or(PrimitiveType::Ring);
		const bool isRing = shape == PrimitiveType::Ring;
		int32_t shapeIndex = isRing ? 0 : 1;
		const char* shapeNames[] = { "Ring", "Cylinder" };
		if (ImGui::Combo("対象形状", &shapeIndex, shapeNames, 2)) {

			params["shape"] = shapeNames[shapeIndex];
			changed = true;
		}
		if (shapeIndex == 0) {
			changed |= DrawRingParamsJson("開始形状", params, "ringStart");
			changed |= DrawRingParamsJson("終了形状", params, "ringEnd");
		} else {
			changed |= DrawCylinderParamsJson("開始形状", params, "cylinderStart");
			changed |= DrawCylinderParamsJson("終了形状", params, "cylinderEnd");
		}
		changed |= EasingComboJson(params, "easingType");
	} else {

		ImGui::TextDisabled("編集UI未対応のモジュールです");
	}
	return changed;
}

void ParticleEffectEditorTool::LoadEffect(const EditorToolContext& context, AssetID effectID) {

	loaded_ = false;
	editingID_ = effectID;
	curveStates_.clear();
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

	// 既定のモジュール構成で新規エフェクトを作る
	ParticleEffectAsset asset{};
	asset.name = createNameBuffer_;
	asset.modules = {
		{ "SizeOverLifetime", nlohmann::json::object() },
		{ "ColorOverLifetime", nlohmann::json::object() },
	};

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