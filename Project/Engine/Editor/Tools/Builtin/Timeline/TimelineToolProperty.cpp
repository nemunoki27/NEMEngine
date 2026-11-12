#include "TimelineTool.h"
#include "TimelineClipFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Animation/Properties/MaterialAnimationPropertyCatalog.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Editor/UI/Inspectors/Common/SimpleAnimationFields.h>

// c++
#include <algorithm>
#include <type_traits>

using namespace Engine;

namespace {

	// プロパティ値を既存の型別JSON表現へ変換する
	template <typename Variant>
	nlohmann::json WritePropertyValue(const Variant& value) {

		return std::visit([](const auto& typed) -> nlohmann::json {
			using T = std::decay_t<decltype(typed)>;
			if constexpr (std::is_arithmetic_v<T>) return typed;
			else return typed.ToJson();
		}, value);
	}

	// 共通の値編集へ渡す型をプロパティ情報から決める
	SimpleAnimationValue ReadPropertyValue(AnimationValueType type, const nlohmann::json& value) {

		switch (type) {
		case AnimationValueType::Float: return value.is_number() ? value.get<float>() : 0.0f;
		case AnimationValueType::Int: return value.is_number() ? value.get<int32_t>() : 0;
		case AnimationValueType::Vector2: return value.is_object() || value.is_array() ? Vector2::FromJson(value) : Vector2{};
		case AnimationValueType::Vector3: return value.is_object() || value.is_array() ? Vector3::FromJson(value) : Vector3{};
		case AnimationValueType::Vector4: return value.is_object() || value.is_array() ? Vector4::FromJson(value) : Vector4{};
		case AnimationValueType::Color3: return value.is_object() || value.is_array() ? Color3::FromJson(value) : Color3{};
		case AnimationValueType::Color4: return value.is_object() || value.is_array() ? Color4::FromJson(value) : Color4{};
		case AnimationValueType::Quaternion: return value.is_object() || value.is_array() ? Quaternion::FromJson(value) : Quaternion{};
		}
		return 0.0f;
	}

	// 回転の編集方式以外は既存SimpleAnimationの編集を共用する
	void DrawPropertyValue(const char* label, nlohmann::json& source, AnimationValueType type, bool euler) {

		auto value = ReadPropertyValue(type, source);
		ValueEditResult result;
		if (euler && std::holds_alternative<Quaternion>(value)) result = MyGUI::DragQuaternion(label, std::get<Quaternion>(value), true);
		else result = SimpleAnimationFields::DrawValue(label, value);
		if (!result.valueChanged) return;
		source = WritePropertyValue(value);
	}
}

void TimelineTool::DrawPropertyClipData(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip) {

	TimelineClipFields::Asset("アニメーションクリップ", clip.data, "asset", context.toolContext.assetDatabase,
		{AssetType::AnimationClip});
	TimelineClipFields::Bool("終了時に元へ戻す", clip.data, "restoreOnEnd", true);
	if (ParseAssetID(clip.data, "asset")) return;
	AnimationPropertyBinding binding;
	if (clip.data.contains("binding")) binding = clip.data.at("binding").get<AnimationPropertyBinding>();
	const Entity target = ResolveTrackTarget(context, track);
	ECSWorld* world = context.GetWorld();
	if (world && world->IsAlive(target)) {
		SetAnimationMaterialContext(*world, context.toolContext.assetDatabase,
			context.toolContext.systemContext ? context.toolContext.systemContext->renderAssetLibrary : nullptr);
		AnimationPropertyQueryContext query;
		query.assetDatabase = context.toolContext.assetDatabase;
		query.renderPipeline = context.panelContext ? context.panelContext->renderPipeline : nullptr;
		const auto properties = AnimationPropertyRegistry::GetInstance().CollectProperties(*world, target, query);
		std::vector<std::string> labels;
		std::vector<const char*> names;
		int selected = -1;
		for (size_t i = 0; i < properties.size(); ++i) {
			labels.push_back(properties[i].componentName + "." + properties[i].propertyPath);
			if (properties[i].componentName == binding.componentName && properties[i].propertyPath == binding.propertyPath)
				selected = static_cast<int>(i);
		}
		for (const auto& label : labels) names.push_back(label.c_str());
		if (!names.empty() && TimelineEditorFields::Select("適用先", selected, names.data(), static_cast<int>(names.size())).valueChanged) {
			const auto& property = properties[selected];
			binding = {property.componentName, property.propertyPath, property.valueType};
			AnimationPropertyValue value;
			if (property.getValue && property.getValue(*world, target, value)) {
				clip.data["from"] = WritePropertyValue(value);
				clip.data["to"] = WritePropertyValue(value);
			}
			clip.data["binding"] = binding;
		}
	}
	if (MyGUI::CollapsingHeader("プロパティの指定", false)) {
		ImGui::Indent();
		bool changed = MyGUI::InputText("コンポーネント", binding.componentName).valueChanged;
		changed |= MyGUI::InputText("プロパティ", binding.propertyPath).valueChanged;
		const char* types[]{"float", "Vector2", "Vector3", "Vector4", "Color3", "Color4", "Quaternion", "int"};
		int type = static_cast<int>(binding.valueType);
		if (TimelineEditorFields::Select("型", type, types, 8).valueChanged) {
			binding.valueType = static_cast<AnimationValueType>(type);
			const auto value = WritePropertyValue(ReadPropertyValue(binding.valueType, nlohmann::json()));
			clip.data["from"] = value; clip.data["to"] = value;
			changed = true;
		}
		if (changed) clip.data["binding"] = binding;
		ImGui::Unindent();
	}
	bool euler = clip.data.value("eulerEdit", true);
	const auto initial = WritePropertyValue(ReadPropertyValue(binding.valueType, nlohmann::json()));
	if (!clip.data.contains("from") || clip.data["from"].is_null()) clip.data["from"] = initial;
	if (!clip.data.contains("to") || clip.data["to"].is_null()) clip.data["to"] = initial;
	if (binding.valueType == AnimationValueType::Quaternion) {
		const char* modes[]{"軸と角度", "オイラー角"};
		int mode = euler ? 1 : 0;
		if (TimelineEditorFields::Select("回転編集", mode, modes, 2).valueChanged) clip.data["eulerEdit"] = euler = mode == 1;
	}
	DrawPropertyValue("開始値", clip.data["from"], binding.valueType, euler);
	DrawPropertyValue("終了値", clip.data["to"], binding.valueType, euler);
}
