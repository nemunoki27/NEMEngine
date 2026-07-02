#include "ScriptInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
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
#include <functional>
#include <unordered_map>

//============================================================================
//	ScriptInspectorDrawer classMethods
//============================================================================
namespace {

	using Kind = Engine::ManagedSerializedFieldKind;

	// List要素のドラッグ並び替え用ペイロード、ドラッグ元の要素indexを運ぶ
	constexpr const char* kListElementDragDropType = "SCRIPT_LIST_ELEMENT";

	// スクリプトの解決状態、型の登録有無で判定しフィールド数では判定しない
	enum class ManagedScriptResolutionReason {

		Resolved,          // 型が登録済みで解決済み
		Unassigned,        // 型ID未設定の空スロット
		TypeNotRegistered, // 型IDはあるが未登録の欠落スクリプト
		SchemaUnavailable, // 解決済みだがスキーマ未取得
	};

	// 型IDから登録レジストリで解決状態を求める
	ManagedScriptResolutionReason ResolveScriptReason(const Engine::ScriptEntry& entry) {

		if (entry.scriptTypeID.empty()) {
			return ManagedScriptResolutionReason::Unassigned;
		}
		const Engine::BehaviorTypeInfo* info =
			Engine::BehaviorTypeRegistry::GetInstance().FindByStableScriptTypeID(entry.scriptTypeID);
		// 未登録なら欠落扱い、値は保持して削除しない
		return info ? ManagedScriptResolutionReason::Resolved
			: ManagedScriptResolutionReason::TypeNotRegistered;
	}

	const char* ResolutionReasonLabel(ManagedScriptResolutionReason reason) {
		switch (reason) {
		case ManagedScriptResolutionReason::Resolved:          return "Resolved";
		case ManagedScriptResolutionReason::Unassigned:        return "Unassigned (scriptTypeID 未設定)";
		case ManagedScriptResolutionReason::TypeNotRegistered: return "TypeNotRegistered (registry に型が無い)";
		case ManagedScriptResolutionReason::SchemaUnavailable: return "SchemaUnavailable";
		default:                                               return "Unknown";
		}
	}

	// 完全修飾名から表示用のクラス名だけ取り出す
	std::string ScriptTypeShortName(const std::string& fullName) {

		const size_t dot = fullName.find_last_of('.');
		return dot == std::string::npos ? fullName : fullName.substr(dot + 1);
	}

	// 種別を保存用の型文字列にする
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
		case Kind::ComponentRef: return "ComponentRef";
		case Kind::Object: return "object";
		case Kind::ManagedReference: return "managedReference";
		default: return "unsupported";
		}
	}

	// アセット種別名を列挙へ変換する
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

	// スキーマから空の既定値を作る
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
		case Kind::ComponentRef:
			return nlohmann::json{ {"entity", nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} }} };
		case Kind::Object: {
			nlohmann::json members = nlohmann::json::object();
			for (const auto& member : field.members) {
				if (member) { members[member->name] = DefaultForKind(*member); }
			}
			return members;
		}
		case Kind::ManagedReference:
			return nlohmann::json{ {"type", ""}, {"value", nlohmann::json::object()} };
		default: return nullptr;
		}
	}

	// 既定値JSONを取得する、無ければ種別ごとの既定値を使う
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

	//--------- Vector/Colorの変換 ------------------------------------------

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

	// float編集設定をスキーマ属性から作る
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

	// ヘッダーの補助
	void DrawHeaderIfAny(const Engine::ManagedFieldSchema& field) {
		if (!field.header.empty()) {
			ImGui::SeparatorText(field.header.c_str());
		}
	}
	// 表示ラベル、Label属性があればそれを使い無ければ変数名を使う
	const std::string& FieldDisplayLabel(const Engine::ManagedFieldSchema& field) {
		return field.label.empty() ? field.name : field.label;
	}
	void DrawTooltipIfAny(const Engine::ManagedFieldSchema& field) {
		if (!field.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", field.tooltip.c_str());
		}
	}

	// 64bit整数やdoubleを精度を保って編集する
	Engine::ValueEditResult DrawTextNumber(const char* label, std::string& text) {
		return Engine::MyGUI::InputText(label, text);
	}

	struct DrawContext {
		const Engine::EditorPanelContext* panel = nullptr;
		Engine::ECSWorld* world = nullptr;
		bool readOnly = false;
	};

	// メンバschema配列の描画、Objectとcollection要素の展開で使う
	Engine::ValueEditResult DrawObjectMembers(nlohmann::json& value,
		const std::vector<std::shared_ptr<Engine::ManagedFieldSchema>>& members, const DrawContext& ctx);

	// 値編集の本体、配列やnullableは再帰する
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

	// 64bit整数を文字列で編集して精度を保つ
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

	// 列挙を数値で保持し未知値でも壊さない
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

	// アセット参照のピッカー、UUIDを保存し欠落でも値を保持する
	Engine::ValueEditResult DrawAssetRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		if (!value.is_object()) { value = nlohmann::json{ {"assetId", ""} }; }
		Engine::AssetID assetID = Engine::FromString16Hex(value.value("assetId", std::string{}));

		const Engine::AssetDatabase* db = (ctx.panel && ctx.panel->editorContext) ? ctx.panel->editorContext->assetDatabase : nullptr;
		Engine::ValueEditResult r = Engine::MyGUI::AssetReferenceField(label, assetID, db,
			{ AssetTypeFromName(field.assetType) });
		if (r.valueChanged) {
			value["assetId"] = assetID ? Engine::ToString(assetID) : std::string{};
		}
		return r;
	}

	// EntityRefのlocalFileIDから現在ワールドの表示名を引く、見つからなければ空
	std::string ResolveEntityRefName(Engine::ECSWorld* world, const std::string& localFileID) {

		if (!world || localFileID.empty()) { return {}; }
		std::string name;
		world->ForEach<Engine::SceneObjectComponent>([&](Engine::Entity e, Engine::SceneObjectComponent& so) {
			if (name.empty() && Engine::ToString(so.localFileID) == localFileID) {
				const Engine::NameComponent* nameComponent = world->TryGetComponent<Engine::NameComponent>(e);
				name = nameComponent ? nameComponent->name : std::string("Entity");
			}
			});
		return name;
	}

	// ドロップ受け入れ判定、trueを返したエンティティだけ参照に設定できる
	using EntityDropFilter = std::function<bool(Engine::ECSWorld&, Engine::Entity)>;

	// エンティティ参照の選択、ドラッグで設定しクリアで解除する
	// dropFilter指定時は判定を通らないエンティティのドロップを受け付けない
	Engine::ValueEditResult DrawEntityRef(const char* label, nlohmann::json& value, const DrawContext& ctx,
		const EntityDropFilter& dropFilter = {}, const char* rejectTooltip = nullptr) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) { value = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} }; }

		const std::string kind = value.value("kind", std::string("Null"));
		const std::string localFileID = value.value("localFileId", std::string{});

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}
		// AssetRefと同じ見た目に合わせる、行幅いっぱいのボタンで未設定はグレーアウトする
		const bool hasValue = !(kind == "Null" || localFileID.empty());
		std::string preview;
		if (!hasValue) {
			preview = "None (Drop entity here)";
		} else {
			// AssetRefと同じくName表示にし、解決できなければ欠落表示にする
			const std::string name = ResolveEntityRefName(ctx.world, localFileID);
			preview = name.empty() ? ("Missing Entity | " + localFileID) : ("Name: " + name);
		}

		ImGui::PushID(label);
		if (!hasValue) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }
		ImGui::Button(preview.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
		if (!hasValue) { ImGui::PopStyleColor(); }
		result.anyItemActive = ImGui::IsItemActive();

		// 解除はClearボタンではなくAssetRefと同じ右クリックの削除メニューで行う
		if (hasValue && ImGui::BeginPopupContextItem("##entityRefDelete")) {
			if (ImGui::MenuItem("削除")) {
				value = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::EndPopup();
		}

		// ドラッグされたエンティティを参照に設定する、フィルタを通らないエンティティは受け付けない
		if (ImGui::BeginDragDropTarget()) {

			// Accept前にpayloadを覗いて判定する、受け入れない場合はハイライトを出さずUnityの禁止表示相当にする
			bool acceptable = true;
			if (dropFilter && ctx.world) {
				const ImGuiPayload* hovered = ImGui::GetDragDropPayload();
				if (hovered && hovered->IsDataType(Engine::IEditorPanel::kHierarchyDragDropPayloadType) &&
					hovered->DataSize == sizeof(Engine::UUID)) {

					Engine::UUID draggedUUID = *static_cast<const Engine::UUID*>(hovered->Data);
					Engine::Entity target = ctx.world->FindByUUID(draggedUUID);
					acceptable = ctx.world->IsAlive(target) && dropFilter(*ctx.world, target);
					if (!acceptable && rejectTooltip) {
						ImGui::SetTooltip("%s", rejectTooltip);
					}
				}
			}

			if (acceptable) {
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
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	// スクリプト参照の選択、所有エンティティと対象スロットを選ぶ
	// コンポーネント参照、所有エンティティを選び型Tのコンポーネントを解決対象にする
	Engine::ValueEditResult DrawComponentRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) {
			value = DefaultForKind(field);
		}
		if (!value.contains("entity") || !value["entity"].is_object()) {
			value["entity"] = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		}

		// 対象コンポーネントを持つエンティティだけドロップを受け入れる
		const std::string rejectTooltip = field.componentType + " を持っていません";
		Engine::ValueEditResult entityResult = DrawEntityRef(label, value["entity"], ctx,
			[&field](Engine::ECSWorld& world, Engine::Entity target) {
				return field.componentType.empty() || world.HasComponent(target, field.componentType);
			}, rejectTooltip.c_str());
		result.valueChanged |= entityResult.valueChanged;
		result.anyItemActive |= entityResult.anyItemActive;
		result.editFinished |= entityResult.editFinished;
		return result;
	}

	// 名前空間を除いた型名にする
	std::string ToShortTypeName(const std::string& typeName) {

		const size_t pos = typeName.find_last_of('.');
		return pos == std::string::npos ? typeName : typeName.substr(pos + 1);
	}

	// スロットがフィールドの対象スクリプト型と一致するか
	bool MatchesFieldScriptType(const Engine::ScriptEntry& slotEntry, const Engine::ManagedFieldSchema& field) {

		if (field.scriptType.empty()) {
			return true;
		}
		if (slotEntry.scriptTypeID.empty()) {
			return false;
		}
		const auto* info = Engine::BehaviorTypeRegistry::GetInstance().FindByStableScriptTypeID(slotEntry.scriptTypeID);
		return info && info->name == field.scriptType;
	}

	Engine::ValueEditResult DrawScriptRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) {
			value = DefaultForKind(field);
		}
		if (!value.contains("entity") || !value["entity"].is_object()) {
			value["entity"] = nlohmann::json{ {"kind", "Null"}, {"sourceAsset", ""}, {"localFileId", ""} };
		}

		// 所有エンティティの参照部分、対象スクリプトを持つエンティティだけドロップを受け入れる
		const std::string rejectTooltip = ToShortTypeName(field.scriptType) + " を持っていません";
		Engine::ValueEditResult ownerResult = DrawEntityRef(label, value["entity"], ctx,
			[&field](Engine::ECSWorld& world, Engine::Entity target) {
				const Engine::ScriptComponent* scripts = world.TryGetComponent<Engine::ScriptComponent>(target);
				if (!scripts) {
					return false;
				}
				for (const Engine::ScriptEntry& slotEntry : scripts->scripts) {
					if (MatchesFieldScriptType(slotEntry, field)) {
						return true;
					}
				}
				return false;
			}, rejectTooltip.c_str());
		if (ownerResult.valueChanged) {
			result.valueChanged = true;
			// 所有エンティティが変わったら旧エンティティのスロット選択を破棄する
			value["scriptSlotId"] = "";
			value["scriptTypeId"] = "";
		}
		result.anyItemActive |= ownerResult.anyItemActive;

		// 所有エンティティ上の同型スロットを選ばせる
		const std::string ownerLocal = value["entity"].value("localFileId", std::string{});
		if (!ownerLocal.empty() && ctx.world) {
			// 対象エンティティをlocalFileIDで探す
			Engine::Entity owner = Engine::Entity::Null();
			ctx.world->ForEach<Engine::SceneObjectComponent>([&](Engine::Entity e, Engine::SceneObjectComponent& so) {
				if (Engine::ToString(so.localFileID) == ownerLocal) { owner = e; }
				});

			if (ctx.world->IsAlive(owner) && ctx.world->HasComponent<Engine::ScriptComponent>(owner)) {

				const auto& scriptComponent = ctx.world->GetComponent<Engine::ScriptComponent>(owner);

				// ドロップ直後は一致スロットが1つだけなら自動選択する、複数あるときだけComboで選ばせる
				if (ownerResult.valueChanged) {
					const Engine::ScriptEntry* matched = nullptr;
					int matchCount = 0;
					for (const Engine::ScriptEntry& slotEntry : scriptComponent.scripts) {
						if (MatchesFieldScriptType(slotEntry, field)) {
							matched = &slotEntry;
							++matchCount;
						}
					}
					if (matchCount == 1) {
						value["scriptSlotId"] = Engine::ToString(matched->scriptSlotID);
						value["scriptTypeId"] = matched->scriptTypeID;
						result.editFinished = true;
					}
				}

				const std::string currentSlot = value.value("scriptSlotId", std::string{});

				if (Engine::MyGUI::BeginPropertyRow("  対象スクリプト")) {
					// 候補は型名で表示する、内部IDは分かりにくいので既定プレビューには出さない
					std::string preview = "未選択";
					for (const Engine::ScriptEntry& slotEntry : scriptComponent.scripts) {
						if (Engine::ToString(slotEntry.scriptSlotID) == currentSlot) {
							preview = ToShortTypeName(slotEntry.lastKnownTypeName);
							break;
						}
					}
					if (ImGui::BeginCombo("##slot", preview.c_str())) {
						// 同型が複数あるときの区別用に候補の通し番号を振る
						int candidateOrder = 0;
						for (const Engine::ScriptEntry& slotEntry : scriptComponent.scripts) {
							// 型が一致するスロットのみ候補にする
							if (!MatchesFieldScriptType(slotEntry, field)) {
								continue;
							}
							++candidateOrder;
							const std::string slotID = Engine::ToString(slotEntry.scriptSlotID);
							// 内部IDは見せず名前空間を除いた型名と通し番号で表示する
							const std::string itemLabel = ToShortTypeName(slotEntry.lastKnownTypeName) + " #" + std::to_string(candidateOrder);
							if (ImGui::Selectable(itemLabel.c_str(), slotID == currentSlot)) {
								value["scriptSlotId"] = slotID;
								value["scriptTypeId"] = slotEntry.scriptTypeID;
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

	// 配列とリスト
	Engine::ValueEditResult DrawCollection(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_array()) { value = nlohmann::json::array(); }
		if (!field.element) {
			ImGui::TextDisabled("%s (要素schema無し)", label);
			return result;
		}

		// TreeNodeのIDは要素数を含めず安定させる、要素追加で開閉状態が消えないようにする
		ImGui::PushID(label);
		const bool open = ImGui::TreeNodeEx("##collection",
			ImGuiTreeNodeFlags_SpanAvailWidth, "%s [%zu]", label, value.size());
		if (open) {

			int removeIndex = -1;
			int moveFrom = -1;
			int moveTo = -1;
			for (size_t i = 0; i < value.size(); ++i) {

				ImGui::PushID(static_cast<int>(i));

				// 要素はTreeNodeで折りたたみ、ノード自体を掴んで別要素のノードへドロップすると並び替えできる
				const bool open = ImGui::TreeNodeEx("##element", ImGuiTreeNodeFlags_None, "要素 %d", static_cast<int>(i));
				if (!ctx.readOnly) {
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
						const int srcIndex = static_cast<int>(i);
						ImGui::SetDragDropPayload(kListElementDragDropType, &srcIndex, sizeof(int));
						ImGui::Text("要素 %d を移動", srcIndex);
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kListElementDragDropType)) {
							if (payload->IsDelivery() && payload->DataSize == sizeof(int)) {
								moveFrom = *static_cast<const int*>(payload->Data);
								moveTo = static_cast<int>(i);
							}
						}
						ImGui::EndDragDropTarget();
					}
					// 削除はノードと同じ行に置く
					ImGui::SameLine();
					if (ImGui::SmallButton("削除")) { removeIndex = static_cast<int>(i); }
				}
				if (open) {

					Engine::ValueEditResult r{};
					if (field.element->kind == Kind::Object) {
						// 要素ノードの中で二重に折りたたまないようメンバを直接展開する
						r = DrawObjectMembers(value[i], field.element->members, ctx);
					} else {
						r = DrawValue(*field.element, value[i], ctx, "値");
					}
					if (r.valueChanged) { result.valueChanged = true; }
					result.anyItemActive |= r.anyItemActive;
					result.editFinished |= r.editFinished;
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			if (!ctx.readOnly) {
				ImGui::Separator();
				if (ImGui::SmallButton("要素追加")) {
					value.push_back(DefaultForKind(*field.element));
					result.valueChanged = true;
					result.editFinished = true;
				}
				if (removeIndex >= 0) {
					value.erase(value.begin() + removeIndex);
					result.valueChanged = true;
					result.editFinished = true;
				}
				// ドラッグした要素がドロップ先のindexへ来るよう挿入し直して並び替える
				if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
					nlohmann::json moved = value[moveFrom];
					value.erase(value.begin() + moveFrom);
					value.insert(value.begin() + moveTo, moved);
					result.valueChanged = true;
					result.editFinished = true;
				}
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
		return result;
	}

	// nullableをトグルと値編集で扱う
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

	// メンバschema配列をまとめて描く共通部、ObjectとManagedReferenceの展開描画で使う
	Engine::ValueEditResult DrawObjectMembers(nlohmann::json& value,
		const std::vector<std::shared_ptr<Engine::ManagedFieldSchema>>& members, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) { value = nlohmann::json::object(); }
		for (const auto& member : members) {

			if (!member || member->isHidden) { continue; }
			// 欠落メンバは既定値で補完する
			if (!value.contains(member->name)) { value[member->name] = DefaultForKind(*member); }

			const std::string memberLabel = member->label.empty() ? member->name : member->label;
			Engine::ValueEditResult r = DrawValue(*member, value[member->name], ctx, memberLabel.c_str());
			if (r.valueChanged) { result.valueChanged = true; }
			result.anyItemActive |= r.anyItemActive;
			result.editFinished |= r.editFinished;
		}
		return result;
	}

	// [Serializable]クラス/構造体を折りたたみでメンバ展開して描く
	Engine::ValueEditResult DrawObject(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (field.members.empty()) {
			ImGui::TextDisabled("%s (編集できるメンバ無し)", label);
			return result;
		}

		ImGui::PushID(label);
		const bool open = ImGui::TreeNodeEx("##object", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label);
		if (open) {
			result = DrawObjectMembers(value, field.members, ctx);
			ImGui::TreePop();
		}
		ImGui::PopID();
		return result;
	}

	// 基底型フィールドへの派生型選択、型Comboと選択型のメンバ編集を描く
	Engine::ValueEditResult DrawManagedReference(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx) {

		Engine::ValueEditResult result{};
		if (!value.is_object()) { value = nlohmann::json{ {"type", ""}, {"value", nlohmann::json::object()} }; }
		if (!value.contains("type") || !value["type"].is_string()) { value["type"] = ""; }
		if (!value.contains("value") || !value["value"].is_object()) { value["value"] = nlohmann::json::object(); }

		ImGui::PushID(label);

		// 派生型の選択、Noneで未設定に戻す
		const std::string currentType = value["type"].get<std::string>();
		if (Engine::MyGUI::BeginPropertyRow(label)) {
			const std::string preview = currentType.empty() ? "None" : ToShortTypeName(currentType);
			if (ImGui::BeginCombo("##managedRefType", preview.c_str())) {
				if (ImGui::Selectable("None", currentType.empty()) && !currentType.empty()) {
					value["type"] = "";
					value["value"] = nlohmann::json::object();
					result.valueChanged = true;
					result.editFinished = true;
				}
				for (const auto& candidate : field.candidates) {
					if (ImGui::Selectable(ToShortTypeName(candidate.type).c_str(), candidate.type == currentType) &&
						candidate.type != currentType) {

						// 型変更時は選択型の既定値で値を作り直す
						nlohmann::json members = nlohmann::json::object();
						for (const auto& member : candidate.members) {
							if (member) { members[member->name] = DefaultForKind(*member); }
						}
						value["type"] = candidate.type;
						value["value"] = std::move(members);
						result.valueChanged = true;
						result.editFinished = true;
					}
				}
				ImGui::EndCombo();
			}
			result.anyItemActive |= ImGui::IsItemActive();
			Engine::MyGUI::EndPropertyRow();
		}

		// 選択型のメンバ編集、候補から外れた保存型は値を保持したまま欠落表示にする
		const std::string selectedType = value["type"].get<std::string>();
		if (!selectedType.empty()) {

			const Engine::ManagedFieldSchema::ReferenceCandidate* selected = nullptr;
			for (const auto& candidate : field.candidates) {
				if (candidate.type == selectedType) {
					selected = &candidate;
					break;
				}
			}
			if (selected) {
				Engine::ValueEditResult r = DrawObjectMembers(value["value"], selected->members, ctx);
				if (r.valueChanged) { result.valueChanged = true; }
				result.anyItemActive |= r.anyItemActive;
				result.editFinished |= r.editFinished;
			} else {
				ImGui::TextDisabled("  Missing type | %s", selectedType.c_str());
			}
		}
		ImGui::PopID();
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
		case Kind::ComponentRef: return DrawComponentRef(label, value, field, ctx);
		case Kind::Object: return DrawObject(label, value, field, ctx);
		case Kind::ManagedReference: return DrawManagedReference(label, value, field, ctx);
		default:
			ImGui::TextDisabled("%s : 未対応の型", label);
			return {};
		}
	}

	// 保存フィールドを新形式へ正規化し移行できない値は未解決として保持する
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

		// 旧形式から新形式へ
		nlohmann::json migrated = nlohmann::json::object();
		migrated["schemaVersion"] = schema.schemaVersion != 0 ? schema.schemaVersion : 2;
		migrated["fields"] = nlohmann::json::object();
		migrated["unresolvedFields"] = nlohmann::json::object();

		// 名前と旧名からスキーマを引く
		std::unordered_map<std::string, const Engine::ManagedFieldSchema*> byName;
		for (const Engine::ManagedFieldSchema& f : schema.fields) {
			byName[f.name] = &f;
			for (const std::string& former : f.formerNames) { byName.emplace(former, &f); }
		}

		for (auto& [name, val] : sf.items()) {
			auto it = byName.find(name);
			if (it != byName.end()) {
				const Engine::ManagedFieldSchema* f = it->second;
				migrated["fields"][f->fieldID] = nlohmann::json{
					{"name", f->name}, {"type", KindToTypeString(f->kind)}, {"value", val} };
			} else {
				// 解決できない旧フィールドは捨てずに保持する
				migrated["unresolvedFields"][name] = val;
			}
		}
		sf = std::move(migrated);
		return true;
	}

	// フィールド値を取得し無ければ既定値で作る
	nlohmann::json& EnsureFieldValue(nlohmann::json& sf, const Engine::ManagedFieldSchema& field) {

		nlohmann::json& fields = sf["fields"];
		if (!fields.contains(field.fieldID) || !fields[field.fieldID].is_object()) {
			fields[field.fieldID] = nlohmann::json{
				{"name", field.name}, {"type", KindToTypeString(field.kind)}, {"value", ParseDefaultValue(field)} };
		}
		nlohmann::json& entry = fields[field.fieldID];
		// 名前と型は最新へ更新する
		entry["name"] = field.name;
		entry["type"] = KindToTypeString(field.kind);
		if (!entry.contains("value")) { entry["value"] = ParseDefaultValue(field); }
		return entry["value"];
	}

	// 編集用フィールドを1つ描画し変更ならtrueを返す
	bool DrawAuthoringField(const Engine::ManagedFieldSchema& field, nlohmann::json& sf,
		const DrawContext& ctx, bool& anyItemActive) {

		if (field.isHidden) {
			return false;
		}
		DrawHeaderIfAny(field);

		nlohmann::json& value = EnsureFieldValue(sf, field);
		const std::string label = FieldDisplayLabel(field);

		bool changed = false;
		if (field.isReadOnly) {
			ImGui::BeginDisabled();
			// 読み取り専用は結果を使わない
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

	// 実行中の値を描画し実体へ即時反映する
	void DrawRuntimeField(const Engine::ManagedFieldSchema& field, nlohmann::json& runtimeState,
		const DrawContext& ctx, Engine::BehaviorHandle handle) {

		if (field.isHidden) {
			return;
		}
		DrawHeaderIfAny(field);

		nlohmann::json& fieldValue = runtimeState[field.fieldID];
		if (fieldValue.is_null() && field.kind != Kind::Nullable) {
			fieldValue = ParseDefaultValue(field);
		}
		const std::string label = FieldDisplayLabel(field);

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
			// 実行中の実体だけへ即時反映する
			Engine::BehaviorSystem::SetRuntimeSerializedField(handle, field.fieldID, fieldValue);
		}
	}

	// スロットID一致の実行中ハンドルを引く
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

	// 未解決フィールドを安全に表示する
	void DrawUnresolved(const nlohmann::json& sf) {

		if (!sf.contains("unresolvedFields") || !sf["unresolvedFields"].is_object() || sf["unresolvedFields"].empty()) {
			return;
		}
		if (ImGui::TreeNodeEx("未解決フィールド", ImGuiTreeNodeFlags_SpanAvailWidth)) {
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
		if (entry.scriptTypeID != resolved.scriptTypeID) {

			entry.scriptTypeID = resolved.scriptTypeID;
			entry.lastKnownTypeName = resolved.typeName;
			entry.serializedFields = nlohmann::json::object();
		}
		return result;
	}

	// スクリプトのドロップで項目を追加する
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

				component.scripts.emplace_back(MakeScriptEntry(resolved.scriptTypeID, resolved.typeName, scriptAsset));
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

	// 実行中値の読み戻しを約10Hzに制限する
	static std::unordered_map<uint64_t, std::pair<std::chrono::steady_clock::time_point, nlohmann::json>> runtimeCache;
	const auto now = std::chrono::steady_clock::now();

	if (!playing && !runtimeCache.empty()) {
		runtimeCache.clear();
	}
	// 容量上限を超えたら古い順に間引く
	constexpr size_t kMaxRuntimeCacheEntries = 64;
	if (runtimeCache.size() > kMaxRuntimeCacheEntries) {
		// 最も古い項目から削除して上限以下へ戻す
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

		// 型の解決状態はレジストリで判定する
		const ManagedScriptResolutionReason resolutionReason = ResolveScriptReason(entry);

		// 未登録のときだけ欠落表示にする
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
				// 型コンボはGUIDを引き直すだけで値は保持し新スキーマで移行する
				const ImTextureID searchIcon = EditorTextureHelper::GetSearchIcon(context.graphicsCore->GetTextureUploadService());
				ValueEditResult result = InspectorDrawerCommon::DrawBehaviorTypeField("型", entry.lastKnownTypeName, searchIcon);
				if (result.valueChanged) {
					if (const BehaviorTypeInfo* info =
						BehaviorTypeRegistry::GetInstance().FindByName(entry.lastKnownTypeName)) {
						entry.scriptTypeID = info->scriptTypeID;
					} else {
						entry.scriptTypeID.clear();
					}
					entry.scriptAsset = {};
					// 既存値は保持し移行できない値は未解決へ残る
				}
				PushEditResult(result, anyItemActive);
				ImGui::Separator();
			}
			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("有効", entry.enabled);
				});

			// 解決済みかはレジストリで判定しフィールド数では判定しない
			const ManagedScriptSchema& schema = runtime.GetScriptSchema(entry.scriptTypeID);
			const bool resolved = (resolutionReason == ManagedScriptResolutionReason::Resolved);

			// 解決済みかつフィールドがある場合のみ描画する
			if (resolved && !schema.fields.empty()) {

				// 移行はメモリ上のみで実際の編集時に確定する
				MigrateAuthoring(entry, schema);

				DrawContext ctx{};
				ctx.panel = &context;
				ctx.world = &world;

				if (playing) {

					// 実行中は実体の値を表示編集し保存しない
					ImGui::TextDisabled("Runtime 値");
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

					// 編集中は編集用の値を扱う
					bool changed = false;
					for (const ManagedFieldSchema& field : schema.fields) {
						changed |= DrawAuthoringField(field, entry.serializedFields, ctx, anyItemActive);
					}
					if (changed) {
						// リビジョンを進めて実行側へ再適用させる
						++entry.serializedRevision;
						RequestCommit();
					}
				}
			} else if (resolutionReason == ManagedScriptResolutionReason::TypeNotRegistered) {
				// 未登録のときだけ欠落表示し値は保持する
				ImGui::TextDisabled("型を解決できません (Missing Script)。値は保持されます。");
				ImGui::BulletText("last known type: %s",
					entry.lastKnownTypeName.empty() ? "(unknown)" : entry.lastKnownTypeName.c_str());
				ImGui::BulletText("scriptTypeID: %s", entry.scriptTypeID.c_str());
				ImGui::BulletText("source asset: %016llx", static_cast<unsigned long long>(entry.scriptAsset.value));
				ImGui::BulletText("slot id: %016llx", static_cast<unsigned long long>(entry.scriptSlotID.value));
				ImGui::BulletText("reason: %s", ResolutionReasonLabel(resolutionReason));
				if (ImGui::SmallButton("GUID をコピー")) {
					ImGui::SetClipboardText(entry.scriptTypeID.c_str());
				}
				ImGui::SameLine();
				ImGui::TextDisabled("型を選び直すと Reassign（上の「型」で型を変更）");
			}

			// 未解決フィールドは保持して表示する
			DrawUnresolved(entry.serializedFields);

			// 並べ替えは同一エンティティ内のスロット順
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
	// スロット並べ替えは入れ替えて確定しUndoできる
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
				// 表示はクラス名のみで内部は完全修飾名を保持する
				if (ImGui::MenuItem(ScriptTypeShortName(info.name).c_str())) {
					draft.scripts.emplace_back(MakeScriptEntry(info.scriptTypeID, info.name));
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
