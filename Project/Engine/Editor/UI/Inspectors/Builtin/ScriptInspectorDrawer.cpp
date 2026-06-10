#include "ScriptInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <charconv>
#include <chrono>
#include <unordered_map>

//============================================================================
//	ScriptInspectorDrawer classMethods
//============================================================================
namespace {

	using Kind = Engine::ManagedSerializedFieldKind;

	// kind を保存用 type 文字列にする（authoring entry の "type" 表示・診断用）
	const char* KindToTypeString(Kind kind) {
		switch (kind) {
		case Kind::Bool: return "bool";
		case Kind::Byte: return "byte";
		case Kind::SByte: return "sbyte";
		case Kind::Short: return "short";
		case Kind::UShort: return "ushort";
		case Kind::Int: return "int";
		case Kind::UInt: return "uint";
		case Kind::Long: return "long";
		case Kind::ULong: return "ulong";
		case Kind::Float: return "float";
		case Kind::Double: return "double";
		case Kind::String: return "string";
		case Kind::Enum: return "enum";
		case Kind::Vector2: return "Vector2";
		case Kind::Vector3: return "Vector3";
		case Kind::Vector4: return "Vector4";
		case Kind::Quaternion: return "Quaternion";
		case Kind::Color3: return "Color3";
		case Kind::Color4: return "Color4";
		case Kind::Nullable: return "nullable";
		case Kind::Array: return "array";
		case Kind::List: return "list";
		case Kind::AssetRef: return "AssetRef";
		case Kind::EntityRef: return "EntityRef";
		case Kind::ScriptRef: return "ScriptRef";
		default: return "unsupported";
		}
	}

	// アセット種別名を AssetType へ（schema の assetType filter 用。依存を増やさず手書き）
	Engine::AssetType AssetTypeFromName(const std::string& name) {
		if (name == "Texture") { return Engine::AssetType::Texture; }
		if (name == "Material") { return Engine::AssetType::Material; }
		if (name == "Mesh") { return Engine::AssetType::Mesh; }
		if (name == "Prefab") { return Engine::AssetType::Prefab; }
		if (name == "Scene") { return Engine::AssetType::Scene; }
		if (name == "Audio") { return Engine::AssetType::Audio; }
		if (name == "Font") { return Engine::AssetType::Font; }
		if (name == "AnimationClip") { return Engine::AssetType::AnimationClip; }
		if (name == "RenderPipeline") { return Engine::AssetType::RenderPipeline; }
		return Engine::AssetType::Unknown;
	}

	// schema field から「空の既定値」を作る（collection の新要素や型不一致時の補填に使う）
	nlohmann::json DefaultForKind(const Engine::ManagedFieldSchema& field) {
		switch (field.kind) {
		case Kind::Bool: return false;
		case Kind::Byte: case Kind::SByte: case Kind::Short: case Kind::UShort:
		case Kind::Int: case Kind::UInt: case Kind::Long: case Kind::ULong:
			return 0;
		case Kind::Float: case Kind::Double: return 0.0;
		case Kind::String: return std::string{};
		case Kind::Enum:
			return field.enumValues.empty() ? nlohmann::json(0) : nlohmann::json(std::stoll(field.enumValues.front()));
		case Kind::Vector2: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f} };
		case Kind::Vector3: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f} };
		case Kind::Vector4: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
		case Kind::Quaternion: return nlohmann::json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 1.0f} };
		case Kind::Color3: return nlohmann::json{ {"r", 0.0f}, {"g", 0.0f}, {"b", 0.0f} };
		case Kind::Color4: return nlohmann::json{ {"r", 0.0f}, {"g", 0.0f}, {"b", 0.0f}, {"a", 1.0f} };
		case Kind::Nullable: return nullptr;
		case Kind::Array: case Kind::List: return nlohmann::json::array();
		case Kind::AssetRef: return nlohmann::json{ {"assetId", ""} };
		case Kind::EntityRef: return nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		case Kind::ScriptRef:
			return nlohmann::json{ {"entity", nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} }},
				{"scriptSlotId", ""}, {"scriptTypeId", ""} };
		default: return nullptr;
		}
	}

	// field の既定値 JSON を取得する（schema の defaultValueJson を parse、無ければ DefaultForKind）
	nlohmann::json ParseDefaultValue(const Engine::ManagedFieldSchema& field) {
		if (!field.defaultValueJson.empty() && field.defaultValueJson != "null") {
			try {
				return nlohmann::json::parse(field.defaultValueJson);
			}
			catch (const nlohmann::json::exception&) {
			}
		}
		return DefaultForKind(field);
	}

	//--------- Vector/Color json <-> 型 --------------------------------------

	Engine::Vector2 ReadVector2(const nlohmann::json& v) {
		Engine::Vector2 r{};
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); }
		return r;
	}
	Engine::Vector3 ReadVector3(const nlohmann::json& v) {
		Engine::Vector3 r{};
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); r.z = v.value("z", 0.0f); }
		return r;
	}
	Engine::Vector4 ReadVector4(const nlohmann::json& v) {
		Engine::Vector4 r{};
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); r.z = v.value("z", 0.0f); r.w = v.value("w", 0.0f); }
		return r;
	}
	Engine::Quaternion ReadQuaternion(const nlohmann::json& v) {
		Engine::Quaternion r = Engine::Quaternion::Identity();
		if (v.is_object()) { r.x = v.value("x", 0.0f); r.y = v.value("y", 0.0f); r.z = v.value("z", 0.0f); r.w = v.value("w", 1.0f); }
		return r;
	}
	Engine::Color3 ReadColor3(const nlohmann::json& v) {
		Engine::Color3 r{};
		if (v.is_object()) { r.r = v.value("r", 0.0f); r.g = v.value("g", 0.0f); r.b = v.value("b", 0.0f); }
		return r;
	}
	Engine::Color4 ReadColor4(const nlohmann::json& v) {
		Engine::Color4 r{};
		if (v.is_object()) { r.r = v.value("r", 0.0f); r.g = v.value("g", 0.0f); r.b = v.value("b", 0.0f); r.a = v.value("a", 1.0f); }
		return r;
	}
	nlohmann::json WriteVector2(const Engine::Vector2& v) { return { {"x", v.x}, {"y", v.y} }; }
	nlohmann::json WriteVector3(const Engine::Vector3& v) { return { {"x", v.x}, {"y", v.y}, {"z", v.z} }; }
	nlohmann::json WriteVector4(const Engine::Vector4& v) { return { {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} }; }
	nlohmann::json WriteQuaternion(const Engine::Quaternion& v) { return { {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} }; }
	nlohmann::json WriteColor3(const Engine::Color3& v) { return { {"r", v.r}, {"g", v.g}, {"b", v.b} }; }
	nlohmann::json WriteColor4(const Engine::Color4& v) { return { {"r", v.r}, {"g", v.g}, {"b", v.b}, {"a", v.a} }; }

	// float drag 設定を schema 属性から作る
	Engine::FloatEditSetting MakeFloatSetting(const Engine::ManagedFieldSchema& field) {
		Engine::FloatEditSetting s{};
		if (field.hasDragSpeed) { s.dragSpeed = field.dragSpeed; }
		if (field.hasMin) { s.minValue = field.minValue; }
		if (field.hasRange) {
			s.minValue = field.rangeMin;
			s.maxValue = field.rangeMax;
			s.flags = ImGuiSliderFlags_AlwaysClamp;
		}
		return s;
	}

	// header / tooltip の補助
	void DrawHeaderIfAny(const Engine::ManagedFieldSchema& field) {
		if (!field.header.empty()) {
			ImGui::SeparatorText(field.header.c_str());
		}
	}
	void DrawTooltipIfAny(const Engine::ManagedFieldSchema& field) {
		if (!field.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", field.tooltip.c_str());
		}
	}

	// 64bit 整数 / double を InputText で精度を保って編集する
	Engine::ValueEditResult DrawTextNumber(const char* label, std::string& text) {
		return Engine::MyGUI::InputText(label, text);
	}

	struct DrawContext {
		const Engine::EditorPanelContext* panel = nullptr;
		Engine::ECSWorld* world = nullptr;
		bool readOnly = false;
	};

	// 値編集の本体。value を in-place で書き換え、変更有無を返す（collection/nullable は再帰）
	Engine::ValueEditResult DrawValue(const Engine::ManagedFieldSchema& field, nlohmann::json& value,
		const DrawContext& ctx, const char* label);

	// 整数（型幅でクランプ）
	Engine::ValueEditResult DrawClampedInt(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, long long lo, long long hi) {

		int v = value.is_number_integer() ? static_cast<int>(value.get<long long>()) : 0;
		Engine::IntEditSetting s{};
		if (field.hasDragSpeed) { s.dragSpeed = field.dragSpeed; }
		s.minValue = static_cast<int32_t>(std::max<long long>(lo, s.minValue));
		s.maxValue = static_cast<int32_t>(std::min<long long>(hi, s.maxValue));
		if (field.hasMin) { s.minValue = static_cast<int32_t>(std::max<long long>(lo, static_cast<long long>(field.minValue))); }
		if (field.hasRange) {
			s.minValue = static_cast<int32_t>(std::max<long long>(lo, static_cast<long long>(field.rangeMin)));
			s.maxValue = static_cast<int32_t>(std::min<long long>(hi, static_cast<long long>(field.rangeMax)));
		}
		Engine::ValueEditResult r = Engine::MyGUI::DragInt(label, v, s);
		if (r.valueChanged) {
			long long clamped = std::clamp<long long>(v, lo, hi);
			value = clamped;
		}
		return r;
	}

	// 64bit 整数（符号付き/無し）を文字列編集で精度維持
	Engine::ValueEditResult DrawLong(const char* label, nlohmann::json& value, bool isUnsigned) {

		std::string text;
		if (isUnsigned) {
			unsigned long long v = value.is_number_unsigned() ? value.get<unsigned long long>()
				: (value.is_number_integer() ? static_cast<unsigned long long>(value.get<long long>()) : 0ull);
			text = std::to_string(v);
		} else {
			long long v = value.is_number_integer() ? value.get<long long>() : 0;
			text = std::to_string(v);
		}
		Engine::ValueEditResult r = DrawTextNumber(label, text);
		if (r.valueChanged) {
			if (isUnsigned) {
				unsigned long long parsed = 0;
				auto res = std::from_chars(text.data(), text.data() + text.size(), parsed);
				if (res.ec == std::errc()) { value = parsed; }
			} else {
				long long parsed = 0;
				auto res = std::from_chars(text.data(), text.data() + text.size(), parsed);
				if (res.ec == std::errc()) { value = parsed; }
			}
		}
		return r;
	}

	// double（float へ落とさない）
	Engine::ValueEditResult DrawDouble(const char* label, nlohmann::json& value) {

		double v = value.is_number() ? value.get<double>() : 0.0;
		std::string text = std::to_string(v);
		Engine::ValueEditResult r = DrawTextNumber(label, text);
		if (r.valueChanged) {
			try { value = std::stod(text); }
			catch (...) {}
		}
		return r;
	}

	// enum（underlying 値を保持。unknown でも破壊しない）
	Engine::ValueEditResult DrawEnum(const char* label, nlohmann::json& value, const Engine::ManagedFieldSchema& field) {

		Engine::ValueEditResult result{};
		long long current = value.is_number_integer() ? value.get<long long>() : 0;

		// 現在値に対応する名前を探す
		int currentIndex = -1;
		for (size_t i = 0; i < field.enumValues.size(); ++i) {
			try {
				if (std::stoll(field.enumValues[i]) == current) { currentIndex = static_cast<int>(i); break; }
			}
			catch (...) {}
		}
		const std::string preview = currentIndex >= 0 ? field.enumNames[currentIndex]
			: ("(" + std::to_string(current) + ")");

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}
		if (ImGui::BeginCombo("##Value", preview.c_str())) {
			for (size_t i = 0; i < field.enumNames.size(); ++i) {
				const bool selected = (static_cast<int>(i) == currentIndex);
				if (ImGui::Selectable(field.enumNames[i].c_str(), selected)) {
					try { value = std::stoll(field.enumValues[i]); result.valueChanged = true; }
					catch (...) {}
				}
				if (selected) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::EndCombo();
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged;
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// AssetRef（typed picker。UUID を保存、Missing でも値は保持）
	Engine::ValueEditResult DrawAssetRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		if (!value.is_object()) { value = nlohmann::json{ {"assetId", ""} }; }
		Engine::AssetID assetId = Engine::FromString16Hex(value.value("assetId", std::string{}));

		const Engine::AssetDatabase* db = (ctx.panel && ctx.panel->editorContext) ? ctx.panel->editorContext->assetDatabase : nullptr;
		Engine::ValueEditResult r = Engine::MyGUI::AssetReferenceField(label, assetId, db,
			{ AssetTypeFromName(field.assetType) });
		if (r.valueChanged) {
			value["assetId"] = assetId ? Engine::ToString(assetId) : std::string{};
		}
		return r;
	}

	// EntityRef（最小 selector：Hierarchy からの drag で設定、Clear で解除、Missing 表示）
	Engine::ValueEditResult DrawEntityRef(const char* label, nlohmann::json& value, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) { value = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} }; }

		const std::string kind = value.value("kind", std::string("Null"));
		const std::string localFileId = value.value("localFileId", std::string{});

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}
		const std::string preview = (kind == "Null" || localFileId.empty())
			? std::string("<None>") : (kind + ":" + localFileId);
		ImGui::Button(preview.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));

		// Hierarchy からの Entity drag を受け取って identity を設定する
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Engine::IEditorPanel::kHierarchyDragDropPayloadType)) {
				if (payload->IsDelivery() && payload->DataSize == sizeof(Engine::UUID) && ctx.world) {

					Engine::UUID draggedUUID = *static_cast<const Engine::UUID*>(payload->Data);
					Engine::Entity target = ctx.world->FindByUUID(draggedUUID);
					if (ctx.world->IsAlive(target) && ctx.world->HasComponent<Engine::SceneObjectComponent>(target)) {

						const auto& sceneObject = ctx.world->GetComponent<Engine::SceneObjectComponent>(target);
						value["kind"] = "Scene";
						value["sourceAsset"] = sceneObject.sourceAsset ? Engine::ToString(sceneObject.sourceAsset) : std::string{};
						value["localFileId"] = Engine::ToString(sceneObject.localFileID);
						result.valueChanged = true;
						result.editFinished = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear")) {
			value = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
			result.valueChanged = true;
			result.editFinished = true;
		}
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// ScriptRef（最小 selector：owner を Hierarchy drag で設定し、対象 type の slot を combo 選択）
	Engine::ValueEditResult DrawScriptRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) {
			value = DefaultForKind(field);
		}
		if (!value.contains("entity") || !value["entity"].is_object()) {
			value["entity"] = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		}

		// owner Entity（EntityRef 部分）
		Engine::ValueEditResult ownerResult = DrawEntityRef(label, value["entity"], ctx);
		if (ownerResult.valueChanged) { result.valueChanged = true; }
		result.anyItemActive |= ownerResult.anyItemActive;

		// owner が設定済みなら、その Entity 上の同 type script slot を選ばせる
		const std::string ownerLocal = value["entity"].value("localFileId", std::string{});
		if (!ownerLocal.empty() && ctx.world) {
			// 対象 Entity を localFileID で探索する
			Engine::Entity owner = Engine::Entity::Null();
			ctx.world->ForEach<Engine::SceneObjectComponent>([&](Engine::Entity e, Engine::SceneObjectComponent& so) {
				if (Engine::ToString(so.localFileID) == ownerLocal) { owner = e; }
				});

			if (ctx.world->IsAlive(owner) && ctx.world->HasComponent<Engine::ScriptComponent>(owner)) {

				const auto& scriptComponent = ctx.world->GetComponent<Engine::ScriptComponent>(owner);
				const std::string currentSlot = value.value("scriptSlotId", std::string{});

				if (Engine::MyGUI::BeginPropertyRow("  slot")) {
					const std::string preview = currentSlot.empty() ? std::string("<None>") : currentSlot;
					if (ImGui::BeginCombo("##slot", preview.c_str())) {
						for (const Engine::ScriptEntry& slotEntry : scriptComponent.scripts) {
							// 型制約 T と一致する slot のみ候補にする（scriptTypeId 一致）
							if (!field.scriptType.empty() && !slotEntry.scriptTypeId.empty()) {
								const auto* info = Engine::BehaviorTypeRegistry::GetInstance()
									.FindByStableScriptTypeID(slotEntry.scriptTypeId);
								if (info && info->name != field.scriptType) {
									continue;
								}
							}
							const std::string slotId = Engine::ToString(slotEntry.scriptSlotID);
							const std::string itemLabel = slotEntry.lastKnownTypeName + " (" + slotId + ")";
							if (ImGui::Selectable(itemLabel.c_str(), slotId == currentSlot)) {
								value["scriptSlotId"] = slotId;
								value["scriptTypeId"] = slotEntry.scriptTypeId;
								result.valueChanged = true;
								result.editFinished = true;
							}
						}
						ImGui::EndCombo();
					}
					Engine::MyGUI::EndPropertyRow();
				}
			}
		}
		return result;
	}

	// 配列 / List
	Engine::ValueEditResult DrawCollection(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_array()) { value = nlohmann::json::array(); }
		if (!field.element) {
			ImGui::TextDisabled("%s (要素schema無し)", label);
			return result;
		}

		const std::string headerLabel = label + std::string(" [") + std::to_string(value.size()) + "]";
		if (ImGui::TreeNodeEx(headerLabel.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {

			int removeIndex = -1;
			int moveFrom = -1;
			int moveTo = -1;
			for (size_t i = 0; i < value.size(); ++i) {

				ImGui::PushID(static_cast<int>(i));
				const std::string elementLabel = std::to_string(i);
				Engine::ValueEditResult r = DrawValue(*field.element, value[i], ctx, elementLabel.c_str());
				if (r.valueChanged) { result.valueChanged = true; }
				result.anyItemActive |= r.anyItemActive;

				if (!ctx.readOnly) {
					ImGui::SameLine();
					if (ImGui::SmallButton("X")) { removeIndex = static_cast<int>(i); }
					ImGui::SameLine();
					if (ImGui::SmallButton("^") && i > 0) { moveFrom = static_cast<int>(i); moveTo = static_cast<int>(i) - 1; }
					ImGui::SameLine();
					if (ImGui::SmallButton("v") && i + 1 < value.size()) { moveFrom = static_cast<int>(i); moveTo = static_cast<int>(i) + 1; }
				}
				ImGui::PopID();
			}

			if (!ctx.readOnly) {
				if (ImGui::SmallButton("+ Add")) {
					value.push_back(DefaultForKind(*field.element));
					result.valueChanged = true;
					result.editFinished = true;
				}
				if (removeIndex >= 0) {
					value.erase(value.begin() + removeIndex);
					result.valueChanged = true;
					result.editFinished = true;
				}
				if (moveFrom >= 0 && moveTo >= 0) {
					std::swap(value[moveFrom], value[moveTo]);
					result.valueChanged = true;
					result.editFinished = true;
				}
			}
			ImGui::TreePop();
		}
		return result;
	}

	// nullable（null トグル + 値 editor）
	Engine::ValueEditResult DrawNullable(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		bool hasValue = !value.is_null();

		if (Engine::MyGUI::BeginPropertyRow(label)) {
			if (ImGui::Checkbox("##hasValue", &hasValue)) {
				if (hasValue && field.element) { value = DefaultForKind(*field.element); }
				else { value = nullptr; }
				result.valueChanged = true;
				result.editFinished = true;
			}
			Engine::MyGUI::EndPropertyRow();
		}
		if (!value.is_null() && field.element) {
			Engine::ValueEditResult r = DrawValue(*field.element, value, ctx, "  value");
			if (r.valueChanged) { result.valueChanged = true; }
			result.anyItemActive |= r.anyItemActive;
		}
		return result;
	}

	Engine::ValueEditResult DrawValue(const Engine::ManagedFieldSchema& field, nlohmann::json& value,
		const DrawContext& ctx, const char* label) {

		using namespace Engine;
		switch (field.kind) {
		case Kind::Bool: {
			bool v = value.is_boolean() ? value.get<bool>() : false;
			ValueEditResult r = InspectorDrawerCommon::DrawCheckboxField(label, v);
			if (r.valueChanged) { value = v; }
			return r;
		}
		case Kind::Byte:   return DrawClampedInt(label, value, field, 0, 255);
		case Kind::SByte:  return DrawClampedInt(label, value, field, -128, 127);
		case Kind::Short:  return DrawClampedInt(label, value, field, -32768, 32767);
		case Kind::UShort: return DrawClampedInt(label, value, field, 0, 65535);
		case Kind::Int:    return DrawClampedInt(label, value, field, INT32_MIN, INT32_MAX);
		case Kind::UInt:   return DrawLong(label, value, true);
		case Kind::Long:   return DrawLong(label, value, false);
		case Kind::ULong:  return DrawLong(label, value, true);
		case Kind::Float: {
			float v = value.is_number() ? value.get<float>() : 0.0f;
			ValueEditResult r = MyGUI::DragFloat(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = v; }
			return r;
		}
		case Kind::Double: return DrawDouble(label, value);
		case Kind::String: {
			std::string v = value.is_string() ? value.get<std::string>() : std::string{};
			TextEditSetting s{};
			s.multiLine = field.multiline;
			ValueEditResult r = MyGUI::InputText(label, v, s);
			if (r.valueChanged) { value = v; }
			return r;
		}
		case Kind::Enum: return DrawEnum(label, value, field);
		case Kind::Vector2: {
			Vector2 v = ReadVector2(value);
			ValueEditResult r = MyGUI::DragVector2(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = WriteVector2(v); }
			return r;
		}
		case Kind::Vector3: {
			Vector3 v = ReadVector3(value);
			ValueEditResult r = MyGUI::DragVector3(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = WriteVector3(v); }
			return r;
		}
		case Kind::Vector4: {
			Vector4 v = ReadVector4(value);
			ValueEditResult r = MyGUI::DragVector4(label, v, MakeFloatSetting(field));
			if (r.valueChanged) { value = WriteVector4(v); }
			return r;
		}
		case Kind::Quaternion: {
			Quaternion v = ReadQuaternion(value);
			ValueEditResult r = MyGUI::DragQuaternion(label, v, true);
			if (r.valueChanged) { value = WriteQuaternion(v); }
			return r;
		}
		case Kind::Color3: {
			Color3 v = ReadColor3(value);
			ValueEditResult r = MyGUI::ColorEdit(label, v);
			if (r.valueChanged) { value = WriteColor3(v); }
			return r;
		}
		case Kind::Color4: {
			Color4 v = ReadColor4(value);
			ValueEditResult r = MyGUI::ColorEdit(label, v);
			if (r.valueChanged) { value = WriteColor4(v); }
			return r;
		}
		case Kind::Nullable: return DrawNullable(label, value, field, ctx);
		case Kind::Array:
		case Kind::List: return DrawCollection(label, value, field, ctx);
		case Kind::AssetRef: return DrawAssetRef(label, value, field, ctx);
		case Kind::EntityRef: return DrawEntityRef(label, value, ctx);
		case Kind::ScriptRef: return DrawScriptRef(label, value, field, ctx);
		default:
			ImGui::TextDisabled("%s : 未対応の型", label);
			return {};
		}
	}

	// ScriptEntry.serializedFields を新形式 { schemaVersion, fields, unresolvedFields } へ正規化する。
	// legacy flat { name: value } は schema の name/formerNames で migrate し、解決不能は unresolvedFields に残す。
	// 既存 GUID は維持し、unknown も捨てない（round-trip）。
	bool MigrateAuthoring(Engine::ScriptEntry& entry, const Engine::ManagedScriptSchema& schema) {

		nlohmann::json& sf = entry.serializedFields;
		if (!sf.is_object()) { sf = nlohmann::json::object(); }

		// 既に新形式
		const bool alreadyNew = sf.contains("fields") && sf["fields"].is_object();
		if (alreadyNew) {
			if (!sf.contains("unresolvedFields") || !sf["unresolvedFields"].is_object()) {
				sf["unresolvedFields"] = nlohmann::json::object();
			}
			sf["schemaVersion"] = schema.schemaVersion != 0 ? schema.schemaVersion : sf.value("schemaVersion", 2);
			return false;
		}

		// legacy flat -> 新形式
		nlohmann::json migrated = nlohmann::json::object();
		migrated["schemaVersion"] = schema.schemaVersion != 0 ? schema.schemaVersion : 2;
		migrated["fields"] = nlohmann::json::object();
		migrated["unresolvedFields"] = nlohmann::json::object();

		// name / formerName -> field schema
		std::unordered_map<std::string, const Engine::ManagedFieldSchema*> byName;
		for (const Engine::ManagedFieldSchema& f : schema.fields) {
			byName[f.name] = &f;
			for (const std::string& former : f.formerNames) { byName.emplace(former, &f); }
		}

		for (auto& [name, val] : sf.items()) {
			auto it = byName.find(name);
			if (it != byName.end()) {
				const Engine::ManagedFieldSchema* f = it->second;
				migrated["fields"][f->fieldId] = nlohmann::json{
					{"name", f->name}, {"type", KindToTypeString(f->kind)}, {"value", val} };
			} else {
				// 解決不能 legacy field は捨てずに保持する
				migrated["unresolvedFields"][name] = val;
			}
		}
		sf = std::move(migrated);
		return true;
	}

	// fields[guid] を取得（無ければ default で作る）して value 参照を返す
	nlohmann::json& EnsureFieldValue(nlohmann::json& sf, const Engine::ManagedFieldSchema& field) {

		nlohmann::json& fields = sf["fields"];
		if (!fields.contains(field.fieldId) || !fields[field.fieldId].is_object()) {
			fields[field.fieldId] = nlohmann::json{
				{"name", field.name}, {"type", KindToTypeString(field.kind)}, {"value", ParseDefaultValue(field)} };
		}
		nlohmann::json& entry = fields[field.fieldId];
		// 現在名/型は最新へ更新（migration 補助。診断用）
		entry["name"] = field.name;
		entry["type"] = KindToTypeString(field.kind);
		if (!entry.contains("value")) { entry["value"] = ParseDefaultValue(field); }
		return entry["value"];
	}

	// 1 schema field を描画する（authoring）。変更されたら true
	bool DrawAuthoringField(const Engine::ManagedFieldSchema& field, nlohmann::json& sf,
		const DrawContext& ctx, bool& anyItemActive) {

		if (field.isHidden) {
			return false;
		}
		DrawHeaderIfAny(field);

		nlohmann::json& value = EnsureFieldValue(sf, field);
		const std::string label = field.name;

		bool changed = false;
		if (field.isReadOnly) {
			ImGui::BeginDisabled();
			Engine::ValueEditResult r = DrawValue(field, value, ctx, label.c_str());
			DrawTooltipIfAny(field);
			ImGui::EndDisabled();
			(void)r; // ReadOnly は編集結果を反映しない
		} else {
			Engine::ValueEditResult r = DrawValue(field, value, ctx, label.c_str());
			DrawTooltipIfAny(field);
			anyItemActive |= r.anyItemActive;
			changed = r.valueChanged;
		}
		return changed;
	}

	// Play中 runtime 値を描画する（live instance へ即時反映。authoring へは保存しない）
	void DrawRuntimeField(const Engine::ManagedFieldSchema& field, nlohmann::json& runtimeState,
		const DrawContext& ctx, Engine::BehaviorHandle handle) {

		if (field.isHidden) {
			return;
		}
		DrawHeaderIfAny(field);

		nlohmann::json& fieldValue = runtimeState[field.fieldId];
		if (fieldValue.is_null() && field.kind != Kind::Nullable) {
			fieldValue = ParseDefaultValue(field);
		}
		const std::string label = field.name;

		if (field.isReadOnly) {
			ImGui::BeginDisabled();
			DrawValue(field, fieldValue, ctx, label.c_str());
			DrawTooltipIfAny(field);
			ImGui::EndDisabled();
			return;
		}
		Engine::ValueEditResult r = DrawValue(field, fieldValue, ctx, label.c_str());
		DrawTooltipIfAny(field);
		if (r.valueChanged) {
			// runtime instance だけへ即時反映（Stop で authoring に戻る）
			Engine::BehaviorSystem::SetRuntimeSerializedField(handle, field.fieldId, fieldValue);
		}
	}

	// entity の live ScriptComponent から、scriptSlotID 一致の entry の BehaviorHandle を引く
	Engine::BehaviorHandle FindLiveHandle(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::UUID& scriptSlotID) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::ScriptComponent>(entity)) {
			return Engine::BehaviorHandle::Null();
		}
		const auto& component = world.GetComponent<Engine::ScriptComponent>(entity);
		for (const Engine::ScriptEntry& entry : component.scripts) {
			if (entry.scriptSlotID == scriptSlotID) {
				return entry.handle;
			}
		}
		return Engine::BehaviorHandle::Null();
	}

	// MissingScript / unresolved field を安全に表示する（値は保持。raw 表示のみ）
	void DrawUnresolved(const nlohmann::json& sf) {

		if (!sf.contains("unresolvedFields") || !sf["unresolvedFields"].is_object() || sf["unresolvedFields"].empty()) {
			return;
		}
		if (ImGui::TreeNodeEx("未解決フィールド (保持中)", ImGuiTreeNodeFlags_SpanAvailWidth)) {
			for (auto& [name, val] : sf["unresolvedFields"].items()) {
				ImGui::TextDisabled("%s = %s", name.c_str(), val.dump().c_str());
			}
			ImGui::TreePop();
		}
	}

	// ScriptEntry を生成する。永続主キーは scriptTypeId、slot ID は必ず発番する
	Engine::ScriptEntry MakeScriptEntry(const std::string& scriptTypeId, const std::string& typeName,
		Engine::AssetID scriptAsset = {}) {

		Engine::ScriptEntry entry{};
		entry.scriptTypeId = scriptTypeId;
		entry.lastKnownTypeName = typeName;
		entry.scriptSlotID = Engine::UUID::New();
		entry.scriptAsset = scriptAsset;
		entry.enabled = true;
		entry.serializedFields = nlohmann::json::object();
		entry.handle = Engine::BehaviorHandle::Null();
		return entry;
	}

	// Scriptアセットの参照フィールドを描画する
	Engine::ValueEditResult DrawScriptAssetField(const Engine::EditorPanelContext& context, Engine::ScriptEntry& entry) {

		Engine::AssetID beforeAsset = entry.scriptAsset;
		Engine::ValueEditResult result = Engine::MyGUI::AssetReferenceField("スクリプト", entry.scriptAsset,
			context.editorContext ? context.editorContext->assetDatabase : nullptr, { Engine::AssetType::Script });
		if (!result.valueChanged) {
			return result;
		}

		Engine::ScriptAssetDragDrop::ResolvedScriptType resolved{};
		if (!Engine::ScriptAssetDragDrop::ResolveScriptType(context, entry.scriptAsset, resolved)) {

			entry.scriptAsset = beforeAsset;
			result.valueChanged = false;
			result.editFinished = false;
			return result;
		}
		if (entry.scriptTypeId != resolved.scriptTypeId) {

			entry.scriptTypeId = resolved.scriptTypeId;
			entry.lastKnownTypeName = resolved.typeName;
			entry.serializedFields = nlohmann::json::object();
		}
		return result;
	}

	// ScriptアセットのドロップでScriptEntryを追加する
	Engine::ValueEditResult DrawScriptDropField(const Engine::EditorPanelContext& context, Engine::ScriptComponent& component) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow("スクリプト")) {
			return result;
		}
		ImGui::Button("C#スクリプトをドロップ", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
		result.anyItemActive = ImGui::IsItemActive();

		if (ImGui::BeginDragDropTarget()) {

			Engine::AssetID scriptAsset{};
			Engine::ScriptAssetDragDrop::ResolvedScriptType resolved{};
			if (Engine::ScriptAssetDragDrop::AcceptScriptAssetDrop(context, scriptAsset, resolved)) {

				component.scripts.emplace_back(MakeScriptEntry(resolved.scriptTypeId, resolved.typeName, scriptAsset));
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::EndDragDropTarget();
		}
		Engine::MyGUI::EndPropertyRow();
		return result;
	}
}

void Engine::ScriptInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	auto& runtime = ManagedScriptRuntime::GetInstance();
	const bool playing = context.IsPlaying();

	// runtime 値の readback throttle（selected entity のみ・約10Hz）
	static std::unordered_map<uint64_t, std::pair<std::chrono::steady_clock::time_point, nlohmann::json>> runtimeCache;
	const auto now = std::chrono::steady_clock::now();

	int32_t removeIndex = -1;
	for (size_t i = 0; i < draft.scripts.size(); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		ScriptEntry& entry = draft.scripts[i];

		// 表示名（解決できなければ Missing Script）
		std::string headerText;
		if (!entry.lastKnownTypeName.empty()) {
			headerText = entry.lastKnownTypeName;
		} else if (!entry.scriptTypeId.empty()) {
			headerText = "Missing Script";
		} else {
			headerText = "Script " + std::to_string(i);
		}

		if (ImGui::TreeNodeEx("##ScriptEntry", ImGuiTreeNodeFlags_DefaultOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth, "%s", headerText.c_str())) {

			{
				ValueEditResult result = DrawScriptAssetField(context, entry);
				PushEditResult(result, anyItemActive);
				ImGui::Separator();
			}
			{
				// 型 combo（選択名から GUID を引き直す）
				ValueEditResult result = InspectorDrawerCommon::DrawBehaviorTypeField("型", entry.lastKnownTypeName);
				if (result.valueChanged) {
					if (const BehaviorTypeInfo* info =
						BehaviorTypeRegistry::GetInstance().FindByName(entry.lastKnownTypeName)) {
						entry.scriptTypeId = info->scriptTypeId;
					} else {
						entry.scriptTypeId.clear();
					}
					entry.scriptAsset = {};
					entry.serializedFields = nlohmann::json::object();
				}
				PushEditResult(result, anyItemActive);
				ImGui::Separator();
			}
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("有効", entry.enabled);
				});

			// schema を取得し、authoring を新形式へ正規化する
			const ManagedScriptSchema& schema = runtime.GetScriptSchema(entry.scriptTypeId);
			const bool resolved = !entry.scriptTypeId.empty() && !schema.fields.empty();

			if (resolved) {

				// 保存形式の migration は draft 上の in-memory のみ。
				// load しただけでは保存せず、実際に値が編集されたとき（下の RequestCommit）に新形式で確定する。
				MigrateAuthoring(entry, schema);

				DrawContext ctx{};
				ctx.panel = &context;
				ctx.world = &world;

				if (playing) {

					// Play: live instance の runtime 値を表示・編集する（authoring には保存しない）
					ImGui::TextDisabled("Runtime 値 (Play中・保存されません)");
					const BehaviorHandle handle = FindLiveHandle(world, entity, entry.scriptSlotID);

					const uint64_t key = (static_cast<uint64_t>(handle.index) << 32) | handle.generation;
					auto& cached = runtimeCache[key];
					if (cached.second.is_null() || (now - cached.first) > std::chrono::milliseconds(100)) {
						cached.first = now;
						cached.second = BehaviorSystem::GetRuntimeSerializedState(handle);
					}
					if (!cached.second.is_object()) { cached.second = nlohmann::json::object(); }

					ctx.readOnly = false;
					for (const ManagedFieldSchema& field : schema.fields) {
						DrawRuntimeField(field, cached.second, ctx, handle);
					}
				} else {

					// Edit: authoring 値を編集する
					bool changed = false;
					for (const ManagedFieldSchema& field : schema.fields) {
						changed |= DrawAuthoringField(field, entry.serializedFields, ctx, anyItemActive);
					}
					if (changed) {
						// revision を進めて、03 の gate で runtime へ再適用させる
						++entry.serializedRevision;
						RequestCommit();
					}
				}
			} else if (!entry.scriptTypeId.empty()) {
				ImGui::TextDisabled("型を解決できません (Missing Script)。値は保持されます。");
			}

			// 未解決フィールドは常に保持し、参照できるよう表示する
			DrawUnresolved(entry.serializedFields);

			if (ImGui::Button("スクリプトを削除")) {
				removeIndex = static_cast<int32_t>(i);
			}
			ImGui::TreePop();
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	// Scriptアセットをドロップして追加する
	DrawField(anyItemActive, [&]() {
		return DrawScriptDropField(context, draft);
		});

	if (0 <= removeIndex) {
		draft.scripts.erase(draft.scripts.begin() + removeIndex);
		RequestCommit();
	}
	if (ImGui::Button("スクリプトを追加")) {
		ImGui::OpenPopup("##AddScriptPopup");
	}

	//============================================================================
	//	スクリプト追加ポップアップ
	//============================================================================
	if (ImGui::BeginPopup("##AddScriptPopup")) {

		const auto& registry = BehaviorTypeRegistry::GetInstance();
		if (registry.GetBehaviorTypeCount() == 0) {
			ImGui::TextDisabled("登録済みビヘイビアはありません。");
		} else {
			for (uint32_t i = 0; i < registry.GetBehaviorTypeCount(); ++i) {

				const auto& info = registry.GetInfo(i);
				if (info.name.empty() || !info.construct) {
					continue;
				}
				if (ImGui::MenuItem(info.name.c_str())) {
					draft.scripts.emplace_back(MakeScriptEntry(info.scriptTypeId, info.name));
					RequestCommit();
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::EndPopup();
	}
	if (draft.scripts.empty()) {
		ImGui::TextDisabled("スクリプトは未設定です。");
	}
}
