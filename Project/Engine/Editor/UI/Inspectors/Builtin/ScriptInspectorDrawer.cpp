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
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Commands/Components/ApplyRuntimeToAuthoringCommand.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <memory>

// c++
#include <charconv>
#include <chrono>
#include <unordered_map>

//============================================================================
//	ScriptInspectorDrawer classMethods
//============================================================================
namespace {

	using Kind = Engine::ManagedSerializedFieldKind;

	// managed scriptの解決状態でtype existenceはregistryを正としschema field数では判定しない、serialized field 0件のvalid ScriptをMissingと誤判定しないため
	enum class ManagedScriptResolutionReason {

		Resolved,        // registry に型があり解決済み、field が 0 件でも resolved
		Unassigned,      // scriptTypeId 未設定の空 slot
		TypeNotRegistered, // scriptTypeId はあるが registry に未登録の Missing Script
		SchemaUnavailable, // 型は解決済みだが schema が未取得で Missing とは別扱い
	};

	// entryのscriptTypeIdをregistryのstable Script type existenceで解決する、schema field数は判定に使わずtypeがregistryに居ればresolvedで0 fieldでもMissingにしない
	ManagedScriptResolutionReason ResolveScriptReason(const Engine::ScriptEntry& entry) {

		if (entry.scriptTypeId.empty()) {
			return ManagedScriptResolutionReason::Unassigned;
		}
		const Engine::BehaviorTypeInfo* info =
			Engine::BehaviorTypeRegistry::GetInstance().FindByStableScriptTypeID(entry.scriptTypeId);
		// registryに型が居なければMissing、payloadは保持して削除しない
		return info ? ManagedScriptResolutionReason::Resolved
			: ManagedScriptResolutionReason::TypeNotRegistered;
	}

	const char* ResolutionReasonLabel(ManagedScriptResolutionReason reason) {
		switch (reason) {
		case ManagedScriptResolutionReason::Resolved:          return "Resolved";
		case ManagedScriptResolutionReason::Unassigned:        return "Unassigned (scriptTypeId 未設定)";
		case ManagedScriptResolutionReason::TypeNotRegistered: return "TypeNotRegistered (registry に型が無い)";
		case ManagedScriptResolutionReason::SchemaUnavailable: return "SchemaUnavailable";
		default:                                               return "Unknown";
		}
	}

	// 完全修飾型名から表示用のクラス名だけ取り出す、識別子はscriptTypeId側が持つ
	std::string ScriptTypeShortName(const std::string& fullName) {

		const size_t dot = fullName.find_last_of('.');
		return dot == std::string::npos ? fullName : fullName.substr(dot + 1);
	}

	// kindを保存用type文字列にする、authoring entryのtype表示と診断用
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

	// アセット種別名をAssetTypeへ変換する、schemaのassetType filter用で依存を増やさず手書き
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

	// schema fieldから空の既定値を作る、collectionの新要素や型不一致時の補填に使う
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

	// fieldの既定値JSONを取得する、schemaのdefaultValueJsonをparseし無ければDefaultForKind
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

	//--------- Vector/Color json <->型--------------------------------------

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

	// float drag設定をschema属性から作る
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

	// header / tooltipの補助
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

	// 64bit整数/ doubleをInputTextで精度を保って編集する
	Engine::ValueEditResult DrawTextNumber(const char* label, std::string& text) {
		return Engine::MyGUI::InputText(label, text);
	}

	struct DrawContext {
		const Engine::EditorPanelContext* panel = nullptr;
		Engine::ECSWorld* world = nullptr;
		bool readOnly = false;
	};

	// 値編集の本体、valueをin-placeで書き換え変更有無を返す、collectionやnullableは再帰
	Engine::ValueEditResult DrawValue(const Engine::ManagedFieldSchema& field, nlohmann::json& value,
		const DrawContext& ctx, const char* label);

	// 整数を型幅でクランプして編集する
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

	// 64bit整数を符号付きと符号無しで文字列編集して精度維持する
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

	// doubleをfloatへ落とさず編集する
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

	// enumをunderlying値で保持しunknownでも破壊しない
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

	// AssetRefのtyped picker、UUIDを保存しMissingでも値は保持する
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

	// EntityRefの最小selector、Hierarchyからのdragで設定しClearで解除してMissingも表示する
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

		// HierarchyからのEntity dragを受け取ってidentityを設定する
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

	// ScriptRefの最小selector、ownerをHierarchy dragで設定し対象typeのslotをcombo選択する
	Engine::ValueEditResult DrawScriptRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) {
			value = DefaultForKind(field);
		}
		if (!value.contains("entity") || !value["entity"].is_object()) {
			value["entity"] = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		}

		// owner EntityのEntityRef部分
		Engine::ValueEditResult ownerResult = DrawEntityRef(label, value["entity"], ctx);
		if (ownerResult.valueChanged) { result.valueChanged = true; }
		result.anyItemActive |= ownerResult.anyItemActive;

		// ownerが設定済みなら、そのEntity上の同type script slotを選ばせる
		const std::string ownerLocal = value["entity"].value("localFileId", std::string{});
		if (!ownerLocal.empty() && ctx.world) {
			// 対象EntityをlocalFileIDで探索する
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
							// 型制約TとscriptTypeIdが一致するslotのみ候補にする
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

	// 配列/ List
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

	// nullableをnullトグルと値editorで編集する
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

	// ScriptEntry.serializedFieldsを新形式schemaVersionとfieldsとunresolvedFieldsへ正規化し、legacy flatなnameからvalueはschemaのnameとformerNamesでmigrateして解決不能はunresolvedFieldsに残す、既存GUIDは維持しunknownも捨てずround-tripさせる
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

		// legacy flat ->新形式
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
				// 解決不能legacy fieldは捨てずに保持する
				migrated["unresolvedFields"][name] = val;
			}
		}
		sf = std::move(migrated);
		return true;
	}

	// fields[guid]を取得し、無ければdefaultで作ってvalue参照を返す
	nlohmann::json& EnsureFieldValue(nlohmann::json& sf, const Engine::ManagedFieldSchema& field) {

		nlohmann::json& fields = sf["fields"];
		if (!fields.contains(field.fieldId) || !fields[field.fieldId].is_object()) {
			fields[field.fieldId] = nlohmann::json{
				{"name", field.name}, {"type", KindToTypeString(field.kind)}, {"value", ParseDefaultValue(field)} };
		}
		nlohmann::json& entry = fields[field.fieldId];
		// 現在の名前と型は最新へ更新する、migration補助と診断用
		entry["name"] = field.name;
		entry["type"] = KindToTypeString(field.kind);
		if (!entry.contains("value")) { entry["value"] = ParseDefaultValue(field); }
		return entry["value"];
	}

	// authoringの1 schema fieldを描画する、変更されたらtrueを返す
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
			// ReadOnlyは編集結果を反映しないため戻り値は使わない
			[[maybe_unused]] Engine::ValueEditResult r = DrawValue(field, value, ctx, label.c_str());
			DrawTooltipIfAny(field);
			ImGui::EndDisabled();
		} else {
			Engine::ValueEditResult r = DrawValue(field, value, ctx, label.c_str());
			DrawTooltipIfAny(field);
			anyItemActive |= r.anyItemActive;
			changed = r.valueChanged;
		}
		return changed;
	}

	// Play中のruntime値を描画する、live instanceへ即時反映しauthoringへは保存しない
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
			// runtime instanceだけへ即時反映しStopでauthoringに戻る
			Engine::BehaviorSystem::SetRuntimeSerializedField(handle, field.fieldId, fieldValue);
		}
	}

	// entityのlive ScriptComponentから、scriptSlotID一致のentryのBehaviorHandleを引く
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

	// MissingScriptやunresolved fieldを安全に表示する、値は保持しraw表示のみ
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

	// Apply Runtime Values To Authoringの専用workflowでruntime entityのstable UUIDからEditWorldの対応entityをlookupしslotをscriptSlotIDで照合する、型互換fieldのみauthoringへmergeしunresolvedや非runtime fieldは保持してauthoring commandでUndo可能に適用、通常のCanEditSceneは広げずEditWorldを直接対象にする
	void ApplyRuntimeValuesToAuthoring(const Engine::EditorPanelContext& context, Engine::ECSWorld& world,
		const Engine::Entity& entity, const Engine::ScriptEntry& entry, const Engine::ManagedScriptSchema& schema,
		const nlohmann::json& runtimeState) {

		Engine::ECSWorld* editWorld = context.editorContext ? context.editorContext->editWorld : nullptr;
		const Engine::UUID stableUUID = world.IsAlive(entity) ? world.GetUUID(entity) : Engine::UUID{};
		const bool canApply = editWorld != nullptr && stableUUID;

		ImGui::BeginDisabled(!canApply);
		const bool clicked = ImGui::Button("Runtime 値を Authoring へ適用");
		ImGui::EndDisabled();
		if (!clicked || !canApply) {
			return;
		}

		// EditWorldの対応entityをstable entity UUIDで解決しruntime handleはdereferenceしない
		const Engine::Entity editEntity = editWorld->FindByUUID(stableUUID);
		if (!editWorld->IsAlive(editEntity) || !editWorld->HasComponent<Engine::ScriptComponent>(editEntity)) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"Apply Runtime Values: EditWorld entity / ScriptComponent not found.");
			return;
		}
		const Engine::ScriptComponent& editComponent = editWorld->GetComponent<Engine::ScriptComponent>(editEntity);

		// slotをscriptSlotIDで照合する、display nameやindexでidentityを決めない
		Engine::ScriptComponent editCopy = editComponent;
		int32_t matched = -1;
		for (size_t s = 0; s < editCopy.scripts.size(); ++s) {
			if (editCopy.scripts[s].scriptSlotID == entry.scriptSlotID) {
				matched = static_cast<int32_t>(s);
				break;
			}
		}
		if (matched < 0) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"Apply Runtime Values: matching authoring slot (scriptSlotID) not found in EditWorld.");
			return;
		}

		// 型互換fieldのみmergeしunresolvedFieldsやruntimeに無いfieldは保持する
		nlohmann::json& sf = editCopy.scripts[matched].serializedFields;
		if (!sf.is_object()) {
			sf = nlohmann::json::object();
		}
		if (!sf.contains("schemaVersion")) {
			sf["schemaVersion"] = 2;
		}
		if (!sf.contains("fields") || !sf["fields"].is_object()) {
			sf["fields"] = nlohmann::json::object();
		}
		if (!sf.contains("unresolvedFields") || !sf["unresolvedFields"].is_object()) {
			sf["unresolvedFields"] = nlohmann::json::object();
		}
		for (const Engine::ManagedFieldSchema& field : schema.fields) {
			const auto it = runtimeState.find(field.fieldId);
			if (it == runtimeState.end() || it->is_null()) {
				continue;
			}
			nlohmann::json fieldEntry;
			fieldEntry["name"] = field.name;
			fieldEntry["type"] = KindToTypeString(field.kind);
			fieldEntry["value"] = *it;
			sf["fields"][field.fieldId] = std::move(fieldEntry);
		}

		const nlohmann::json before = editComponent;
		const nlohmann::json after = editCopy;
		if (before == after) {
			return;
		}
		if (context.host) {
			context.host->ExecuteEditorCommand(std::make_unique<Engine::ApplyRuntimeToAuthoringCommand>(
				stableUUID, "Script", before, after));
		}
	}
}

void Engine::ScriptInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	auto& runtime = ManagedScriptRuntime::GetInstance();
	const bool playing = context.IsPlaying();

	// runtime値のreadback throttle、selected entityのみで約10Hzのbounded cache
	static std::unordered_map<uint64_t, std::pair<std::chrono::steady_clock::time_point, nlohmann::json>> runtimeCache;
	const auto now = std::chrono::steady_clock::now();

	// runtime cacheのlifecycle管理でPlay停止や非Playではruntime値が無効なので捨てreloadでPlayから抜けた直後も含む、これでPlay stopやassembly reloadやscene changeでpruneされる
	if (!playing && !runtimeCache.empty()) {
		runtimeCache.clear();
	}
	// 容量上限、selected entityを跨いだ蓄積で無制限growthしないよう超過時は古い順に間引く
	constexpr size_t kMaxRuntimeCacheEntries = 64;
	if (runtimeCache.size() > kMaxRuntimeCacheEntries) {
		// 最も古いreadbackから削除して上限以下へ戻す、O(n)だが構造変更時のみでhot pathではない
		while (runtimeCache.size() > kMaxRuntimeCacheEntries) {
			auto oldest = runtimeCache.begin();
			for (auto it = runtimeCache.begin(); it != runtimeCache.end(); ++it) {
				if (it->second.first < oldest->second.first) {
					oldest = it;
				}
			}
			runtimeCache.erase(oldest);
		}
	}

	int32_t removeIndex = -1;
	int32_t moveUpIndex = -1;
	int32_t moveDownIndex = -1;
	for (size_t i = 0; i < draft.scripts.size(); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		ScriptEntry& entry = draft.scripts[i];

		// 型解決状態はregistryのstable Script type existenceで判定しschema field数に依存しない
		const ManagedScriptResolutionReason resolutionReason = ResolveScriptReason(entry);

		// 表示名はregistryに型が無いときだけMissing Scriptにする
		std::string headerText;
		if (resolutionReason == ManagedScriptResolutionReason::TypeNotRegistered) {
			headerText = entry.lastKnownTypeName.empty()
				? "Missing Script"
				: ("Missing Script (" + ScriptTypeShortName(entry.lastKnownTypeName) + ")");
		} else if (!entry.lastKnownTypeName.empty()) {
			headerText = ScriptTypeShortName(entry.lastKnownTypeName);
		} else if (resolutionReason == ManagedScriptResolutionReason::Unassigned) {
			headerText = "Script " + std::to_string(i);
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
				// 型comboは選択名からGUIDを引き直すReassignでserializedFieldsはclearせず、新schemaでのMigrateAuthoringがnameやformerNames一致fieldを移行し移行できない値はunresolvedFieldsとして保持してpayloadを失わない
				const ImTextureID searchIcon = EditorTextureHelper::GetSearchIcon(context.graphicsCore->GetTextureUploadService());
				ValueEditResult result = InspectorDrawerCommon::DrawBehaviorTypeField("型", entry.lastKnownTypeName, searchIcon);
				if (result.valueChanged) {
					if (const BehaviorTypeInfo* info =
						BehaviorTypeRegistry::GetInstance().FindByName(entry.lastKnownTypeName)) {
						entry.scriptTypeId = info->scriptTypeId;
					} else {
						entry.scriptTypeId.clear();
					}
					entry.scriptAsset = {};
					// 既存のserialized値は保持し、新形式ならmigrationが走って旧type固有の値はunresolvedに残る
				}
				PushEditResult(result, anyItemActive);
				ImGui::Separator();
			}
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("有効", entry.enabled);
				});

			// 型はregistryで解決済みかを見てschema field数では判定しない、解決済みならserialized fieldが0件でもMissing扱いにしない
			const ManagedScriptSchema& schema = runtime.GetScriptSchema(entry.scriptTypeId);
			const bool resolved = (resolutionReason == ManagedScriptResolutionReason::Resolved);

			// fieldを描画するのは解決済みかつschemaにfieldがある場合のみで0 fieldのvalid Scriptはここを通らないがMissingメッセージも出さない
			if (resolved && !schema.fields.empty()) {

				// 保存形式のmigrationはdraft上のin-memoryのみでloadしただけでは保存せず実際に値が編集されたとき下のRequestCommitで新形式に確定する
				MigrateAuthoring(entry, schema);

				DrawContext ctx{};
				ctx.panel = &context;
				ctx.world = &world;

				if (playing) {

					// Playではlive instanceのruntime値を表示編集しauthoringには保存しない
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

					// Apply Runtime Values To Authoringの専用workflowでCanEditSceneは広げずruntime entityのstable UUIDでEditWorldの対応entityとslotを照合しauthoring commandで適用する
					ApplyRuntimeValuesToAuthoring(context, world, entity, entry, schema, cached.second);
				} else {

					// Edit: authoring値を編集する
					bool changed = false;
					for (const ManagedFieldSchema& field : schema.fields) {
						changed |= DrawAuthoringField(field, entry.serializedFields, ctx, anyItemActive);
					}
					if (changed) {
						// revisionを進めてlifecycle syncのgateでruntimeへ再適用させる
						++entry.serializedRevision;
						RequestCommit();
					}
				}
			} else if (resolutionReason == ManagedScriptResolutionReason::TypeNotRegistered) {
				// registryに型が無い場合のみMissing表示しpayloadは保持して自動削除しない
				ImGui::TextDisabled("型を解決できません (Missing Script)。値は保持されます。");
				ImGui::BulletText("last known type: %s",
					entry.lastKnownTypeName.empty() ? "(unknown)" : entry.lastKnownTypeName.c_str());
				ImGui::BulletText("scriptTypeId: %s", entry.scriptTypeId.c_str());
				ImGui::BulletText("source asset: %016llx", static_cast<unsigned long long>(entry.scriptAsset.value));
				ImGui::BulletText("slot id: %016llx", static_cast<unsigned long long>(entry.scriptSlotID.value));
				ImGui::BulletText("reason: %s", ResolutionReasonLabel(resolutionReason));
				if (ImGui::SmallButton("GUID をコピー")) {
					ImGui::SetClipboardText(entry.scriptTypeId.c_str());
				}
				ImGui::SameLine();
				ImGui::TextDisabled("型を選び直すと Reassign（上の「型」で型を変更）");
			}

			// 未解決フィールドは常に保持し、参照できるよう表示する
			DrawUnresolved(entry.serializedFields);

			// 並べ替えはexecution orderとは別で同一Entity内のslot順、draftを入れ替えてcommitする
			ImGui::BeginDisabled(i == 0);
			if (ImGui::SmallButton("▲")) {
				moveUpIndex = static_cast<int32_t>(i);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 >= draft.scripts.size());
			if (ImGui::SmallButton("▼")) {
				moveDownIndex = static_cast<int32_t>(i);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
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
	// slot並べ替えはdraftを入れ替えてcommitしSetSerializedComponentCommandでUndo Redo可能
	else if (0 < moveUpIndex && moveUpIndex < static_cast<int32_t>(draft.scripts.size())) {
		std::swap(draft.scripts[moveUpIndex], draft.scripts[moveUpIndex - 1]);
		RequestCommit();
	} else if (0 <= moveDownIndex && moveDownIndex + 1 < static_cast<int32_t>(draft.scripts.size())) {
		std::swap(draft.scripts[moveDownIndex], draft.scripts[moveDownIndex + 1]);
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
				// メニュー表示はクラス名のみでentryには完全修飾名を保持する
				if (ImGui::MenuItem(ScriptTypeShortName(info.name).c_str())) {
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
