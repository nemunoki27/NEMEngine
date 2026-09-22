#pragma once

#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Behavior/BehaviorHandle.h>

#include <functional>

namespace Engine::ScriptFieldInspector {

	using Kind = ManagedSerializedFieldKind;
	using EntityDropFilter = std::function<bool(ECSWorld&, Entity)>;
	inline constexpr const char* kListElementDragDropType = "SCRIPT_LIST_ELEMENT";

	enum class ManagedScriptResolutionReason {

		Resolved,          // 型が登録済みで解決済み
		Unassigned,        // 型ID未設定の空スロット
		BuildFailed,       // 最新ビルド失敗により型を更新できない
		TypeNotRegistered, // 型IDはあるが未登録の欠落スクリプト
		SchemaUnavailable, // 解決済みだがスキーマ未取得
	};

	struct DrawContext {
		const Engine::EditorPanelContext* panel = nullptr;
		Engine::ECSWorld* world = nullptr;
		bool readOnly = false;
	};

	// 型と診断の解決
	ManagedScriptResolutionReason ResolveScriptReason(const Engine::ScriptEntry& entry,
		bool sourceBuildFailed, bool globalBuildFailed);
	const Engine::ManagedBuildDiagnostic* FindSourceBuildError(const Engine::ScriptEntry& entry,
		const Engine::AssetDatabase* assetDatabase, uint64_t buildID);
	const Engine::ManagedBuildDiagnostic* FindGlobalBuildError(uint64_t buildID);
	const char* ResolutionReasonLabel(ManagedScriptResolutionReason reason);
	std::string ScriptTypeShortName(const std::string& fullName);

	// 保存Fieldの型と既定値
	const char* KindToTypeString(Kind kind);
	Engine::AssetType AssetTypeFromName(const std::string& name);
	nlohmann::json DefaultForKind(const Engine::ManagedFieldSchema& field);
	nlohmann::json ParseDefaultValue(const Engine::ManagedFieldSchema& field);
	Engine::Vector2 ReadVector2(const nlohmann::json& v);
	Engine::Vector3 ReadVector3(const nlohmann::json& v);
	Engine::Vector4 ReadVector4(const nlohmann::json& v);
	Engine::Quaternion ReadQuaternion(const nlohmann::json& v);
	Engine::Color3 ReadColor3(const nlohmann::json& v);
	Engine::Color4 ReadColor4(const nlohmann::json& v);
	nlohmann::json WriteVector2(const Engine::Vector2& v);
	nlohmann::json WriteVector3(const Engine::Vector3& v);
	nlohmann::json WriteVector4(const Engine::Vector4& v);
	nlohmann::json WriteQuaternion(const Engine::Quaternion& v);
	nlohmann::json WriteColor3(const Engine::Color3& v);
	nlohmann::json WriteColor4(const Engine::Color4& v);
	void EnsureAuthoringSchema(Engine::ScriptEntry& entry, const Engine::ManagedScriptSchema& schema);
	nlohmann::json& EnsureFieldValue(nlohmann::json& sf, const Engine::ManagedFieldSchema& field);

	// 参照Fieldの編集
	Engine::ValueEditResult DrawAssetRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx);
	std::string ResolveEntityRefName(Engine::ECSWorld* world, const std::string& localFileID);
	Engine::ValueEditResult DrawEntityRef(const char* label, nlohmann::json& value, const DrawContext& ctx,
		const EntityDropFilter& dropFilter = {}, const char* rejectTooltip = nullptr);
	Engine::ValueEditResult DrawComponentRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx);
	std::string ToShortTypeName(const std::string& typeName);
	bool MatchesFieldScriptType(const Engine::ScriptEntry& slotEntry, const Engine::ManagedFieldSchema& field);
	Engine::ValueEditResult DrawScriptRef(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx);

	// 集合とオブジェクトの編集
	Engine::ValueEditResult DrawCollection(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx);
	Engine::ValueEditResult DrawNullable(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx);
	Engine::ValueEditResult DrawObjectMembers(nlohmann::json& value,
		const std::vector<std::shared_ptr<Engine::ManagedFieldSchema>>& members, const DrawContext& ctx);
	Engine::ValueEditResult DrawObject(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx);
	Engine::ValueEditResult DrawManagedReference(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, const DrawContext& ctx);

	// Field値の編集
	Engine::FloatEditSetting MakeFloatSetting(const Engine::ManagedFieldSchema& field);
	void DrawHeaderIfAny(const Engine::ManagedFieldSchema& field);
	const std::string& FieldDisplayLabel(const Engine::ManagedFieldSchema& field);
	void DrawTooltipIfAny(const Engine::ManagedFieldSchema& field);
	Engine::ValueEditResult DrawTextNumber(const char* label, std::string& text);
	Engine::ValueEditResult DrawClampedInt(const char* label, nlohmann::json& value,
		const Engine::ManagedFieldSchema& field, long long lo, long long hi);
	Engine::ValueEditResult DrawLong(const char* label, nlohmann::json& value, bool isUnsigned);
	Engine::ValueEditResult DrawDouble(const char* label, nlohmann::json& value);
	Engine::ValueEditResult DrawEnum(const char* label, nlohmann::json& value, const Engine::ManagedFieldSchema& field);
	Engine::ValueEditResult DrawValue(const Engine::ManagedFieldSchema& field, nlohmann::json& value,
		const DrawContext& ctx, const char* label);

	// 編集値と実行値への接続
	bool DrawAuthoringField(const Engine::ManagedFieldSchema& field, nlohmann::json& sf,
		const DrawContext& ctx, bool& anyItemActive);
	void DrawRuntimeField(const Engine::ManagedFieldSchema& field, nlohmann::json& runtimeState,
		const DrawContext& ctx, Engine::BehaviorHandle handle);
	Engine::BehaviorHandle FindLiveHandle(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::UUID& scriptSlotID);
	void DrawUnresolved(const nlohmann::json& sf);
}
