#include "ImGuiHelpers.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

//============================================================================
//	MyGUI classMethods
//============================================================================
namespace {

	//============================================================================
	//	レイアウト定数
	//============================================================================
	// 左側に表示する文字の幅
	constexpr float kLabelColumnWidth = 168.0f;
	constexpr float kAxisLabelWidth = 14.0f;

	// プロパティグループのラベル幅
	struct PropertyLabelWidthState {

		ImGuiID id = 0;
		float labelWidth = 0.0f;
		float measuredWidth = 0.0f;
	};
	std::vector<PropertyLabelWidthState> propertyLabelWidthStack{};
	std::unordered_map<ImGuiID, float> propertyLabelWidthCache{};
	const std::function<void()>* propertyLabelContextMenu = nullptr;

	// プロパティグループのラベル幅計測を開始する
	void BeginPropertyLabelWidth(const char* id) {

		const ImGuiID scopeID = ImGui::GetID(id);
		const auto it = propertyLabelWidthCache.find(scopeID);
		PropertyLabelWidthState state{};
		state.id = scopeID;
		state.labelWidth = it != propertyLabelWidthCache.end() ? it->second : 0.0f;
		propertyLabelWidthStack.emplace_back(state);
	}

	// プロパティグループのラベル幅計測を終了する
	void EndPropertyLabelWidth() {

		if (propertyLabelWidthStack.empty()) { return; }
		const PropertyLabelWidthState state = propertyLabelWidthStack.back();
		propertyLabelWidthStack.pop_back();
		propertyLabelWidthCache[state.id] = state.measuredWidth;
	}

	//============================================================================
	//	軸情報
	//============================================================================
	struct AxisDisplayInfo {

		const char* name;
		ImVec4 color;
	};
	// 軸に対応する表示情報を取得する
	AxisDisplayInfo GetAxisDisplayInfo(char axis) {

		switch (axis) {
		case 'X': return { "X", ImVec4(1.0f, 0.1f, 0.1f, 1.0f) }; // red
		case 'Y': return { "Y", ImVec4(0.1f, 0.3f, 1.0f, 1.0f) }; // blue
		case 'Z': return { "Z", ImVec4(0.1f, 1.0f, 0.3f, 1.0f) }; // green
		case 'W': return { "W", ImVec4(0.90f, 0.78f, 0.20f, 1.0f) }; // yellow
		default:  return { "-", ImVec4(0.70f, 0.70f, 0.70f, 1.0f) };
		}
	}
	//============================================================================
	//	文字列ヘルパー
	//============================================================================
	// 精度を指定してfloatを文字列に変換する
	std::string FormatFloat(float value, uint32_t precision) {

		return std::format("{:.{}f}", value, precision);
	}
	//============================================================================
	//	レイアウトヘルパー
	//============================================================================
	// フィールドの幅を計算する
	float CalcFieldWidth(int fieldCount, float reserveRightWidth = 0.0f) {

		const float avail = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - reserveRightWidth);
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float fieldArea = (std::max)(1.0f, avail - spacing * static_cast<float>((std::max)(0, fieldCount - 1)));
		return fieldArea / static_cast<float>(fieldCount);
	}

	//============================================================================
	//	描画ヘルパー
	//============================================================================
	// 軸ラベルを描画する
	void DrawAxisLabel(char axis) {

		const AxisDisplayInfo info = GetAxisDisplayInfo(axis);
		ImGui::PushStyleColor(ImGuiCol_Text, info.color);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(info.name);
		ImGui::PopStyleColor();
	}
	// コピー可能な値ボックスを描画する
	void DrawCopyableValueBox(const char* id, char axis, const std::string& valueText, float width) {

		ImGui::PushID(id);

		ImGui::BeginGroup();

		// 軸ラベル
		DrawAxisLabel(axis);
		ImGui::SameLine(0.0f, 6.0f);

		const ImVec2 buttonSize(width - kAxisLabelWidth - 6.0f, ImGui::GetFrameHeight());
		if (ImGui::Button(valueText.c_str(), buttonSize)) {
			ImGui::SetClipboardText(valueText.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("クリックでコピー");
		}
		ImGui::EndGroup();
		ImGui::PopID();
	}
	// 複数のコピー可能な値フィールドを描画する
	void DrawTextFields(const char* label, const std::array<char, 4>& axes,
		const float* values, int count, uint32_t precision) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return;
		}

		const float fieldWidth = CalcFieldWidth(count);
		for (int i = 0; i < count; ++i) {

			if (i > 0) {
				ImGui::SameLine();
			}
			const std::string text = FormatFloat(values[i], precision);
			const std::string id = std::format("{}_{}", label, i);
			DrawCopyableValueBox(id.c_str(), axes[i], text, fieldWidth);
		}
		Engine::MyGUI::EndPropertyRow();
	}
	// 単一のコピー可能な値フィールドを描画する
	void DrawScalarTextField(const char* label, float value, uint32_t precision) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return;
		}

		const std::string text = FormatFloat(value, precision);
		if (ImGui::Button(text.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {
			ImGui::SetClipboardText(text.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Click to copy");
		}
		Engine::MyGUI::EndPropertyRow();
	}
	// Engine::SceneViewManipulatorModeをImGuizmo::OPERATIONに変換する
	ImGuizmo::OPERATION ToImGuizmoOperation(Engine::SceneViewManipulatorMode mode) {

		switch (mode) {
		case Engine::SceneViewManipulatorMode::Translate: return ImGuizmo::TRANSLATE;
		case Engine::SceneViewManipulatorMode::Rotate:    return ImGuizmo::ROTATE;
		case Engine::SceneViewManipulatorMode::Scale:     return ImGuizmo::SCALE;
		case Engine::SceneViewManipulatorMode::None:
		default:
			break;
		}
		return static_cast<ImGuizmo::OPERATION>(0);
	}
	// 2Dアフィン行列を平行移動とZ軸のみの回転と拡縮に分解する
	bool DecomposeAffine2D(const Engine::Matrix4x4& matrix, Engine::Vector3& outPos, float& outRotationZ, Engine::Vector3& outScale) {

		constexpr float kEps = 1e-6f;

		outPos = matrix.GetTranslationValue();
		if (!std::isfinite(outPos.x) || !std::isfinite(outPos.y) || !std::isfinite(outPos.z)) {
			return false;
		}

		Engine::Vector2 axisX(matrix.m[0][0], matrix.m[0][1]);
		Engine::Vector2 axisY(matrix.m[1][0], matrix.m[1][1]);

		outScale.x = axisX.Length();
		outScale.y = axisY.Length();
		outScale.z = 1.0f;

		if (!std::isfinite(outScale.x) || !std::isfinite(outScale.y)) {
			return false;
		}
		if (outScale.x <= kEps || outScale.y <= kEps) {
			return false;
		}

		axisX /= outScale.x;
		axisY /= outScale.y;

		const float det = axisX.x * axisY.y - axisX.y * axisY.x;
		if (!std::isfinite(det)) {
			return false;
		}

		if (det < 0.0f) {

			outScale.y = -outScale.y;
			axisY = -axisY;
		}

		outRotationZ = Math::RadToDeg(std::atan2(axisX.y, axisX.x));
		if (!std::isfinite(outRotationZ)) {
			return false;
		}

		return true;
	}
	// 2D用にQuaternionからZ回転だけを安全に取り出す
	float ExtractRotationZDegrees2D(const Engine::Quaternion& rotation) {

		constexpr float kEps = 1e-6f;

		Engine::Quaternion normalized = Engine::Quaternion::Normalize(rotation);
		Engine::Matrix4x4 rotateMatrix = Engine::Quaternion::MakeRotateMatrix(normalized);

		// row-vector前提で、ローカルX軸のXY成分からZ回転を求める
		Engine::Vector2 axisX(rotateMatrix.m[0][0], rotateMatrix.m[0][1]);
		const float len = axisX.Length();
		if (len <= kEps || !std::isfinite(len)) {
			return 0.0f;
		}

		axisX /= len;

		const float angle = std::atan2(axisX.y, axisX.x);
		if (!std::isfinite(angle)) {
			return 0.0f;
		}

		return Math::RadToDeg(angle);
	}
	// ギズモ操作を開始する
	void BeginGizmoManipulate([[maybe_unused]] const char* id, const Engine::GizmoViewContext& context, bool is2DTarget) {

		//ImGuizmo::PushID(id);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(context.rect.x, context.rect.y, context.rect.width, context.rect.height);
		ImGuizmo::SetOrthographic(context.orthographic);
		ImGuizmo::AllowAxisFlip(context.allowAxisFlip);

		if (is2DTarget) {
			//ImGuizmo::SetAxisMask(false, false, true);
		} else {
			//ImGuizmo::SetAxisMask(false, false, false);
		}
	}
	// ギズモ操作を終了する
	void EndGizmoManipulate() {

		//ImGuizmo::PopID();
	}
}

Engine::MyGUI::ScopedPropertyLabelWidth::ScopedPropertyLabelWidth(const char* id) {

	BeginPropertyLabelWidth(id);
}

Engine::MyGUI::ScopedPropertyLabelWidth::~ScopedPropertyLabelWidth() {

	EndPropertyLabelWidth();
}

Engine::MyGUI::ScopedPropertyLabelContextMenu::ScopedPropertyLabelContextMenu(std::function<void()> callback)
	: callback_(std::move(callback)), previous_(propertyLabelContextMenu) {

	propertyLabelContextMenu = &callback_;
}

Engine::MyGUI::ScopedPropertyLabelContextMenu::~ScopedPropertyLabelContextMenu() {

	propertyLabelContextMenu = previous_;
}

bool Engine::MyGUI::CollapsingHeader(const char* label, bool stratOpen) {

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

	ImGuiTreeNodeFlags flags = {};
	if (stratOpen) {

		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	bool open = ImGui::CollapsingHeader(label, flags);

	ImGui::PopStyleVar(3);

	return open;
}

Engine::TextInputPopupResult Engine::MyGUI::InputTextPopupContent(const char* label, std::string& text, const char* errorText) {

	TextInputPopupResult result{};

	ImGui::TextUnformatted(label);
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

	const bool submittedByEnter = ImGui::InputText("##InputTextPopupValue", &text, ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere(-1);
	}

	if (errorText && errorText[0] != '\0') {
		ImGui::TextColored(ImVec4(0.95f, 0.32f, 0.24f, 1.0f), "%s", errorText);
	} else {
		ImGui::Spacing();
	}

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(96.0f, 0.0f)) || submittedByEnter) {
		result.submitted = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		result.canceled = true;
	}
	return result;
}

bool Engine::MyGUI::BeginPropertyRow(const char* label, const PropertyRowSetting& setting) {

	float labelWidth = kLabelColumnWidth;
	if (setting.labelWidth.has_value()) {
		labelWidth = setting.labelWidth.value();
	} else if (!propertyLabelWidthStack.empty()) {

		PropertyLabelWidthState& state = propertyLabelWidthStack.back();
		const float measuredWidth =
			ImGui::CalcTextSize(label).x + 4.0f;
		state.measuredWidth = (std::max)(state.measuredWidth, measuredWidth);
		labelWidth = (std::max)(state.labelWidth, measuredWidth);
	}

	const std::string tableID = std::string("##MyGUI_RowTable_Public_") + label;
	const ImVec2 tableSize = setting.rowWidth.has_value() ?
		ImVec2(setting.rowWidth.value(), 0.0f) : ImVec2(0.0f, 0.0f);
	if (!ImGui::BeginTable(tableID.c_str(), 2,
		ImGuiTableFlags_SizingStretchProp |
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_NoSavedSettings, tableSize)) {
		return false;
	}

	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

	ImGui::TableNextRow();

	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	if (propertyLabelContextMenu && *propertyLabelContextMenu) {

		ImGui::PushID(label);
		if (ImGui::BeginPopupContextItem("##PropertyLabelContextMenu")) {
			(*propertyLabelContextMenu)();
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	ImGui::TableSetColumnIndex(1);
	ImGui::PushID(label);
	return true;
}

void Engine::MyGUI::EndPropertyRow() {

	ImGui::PopID();
	ImGui::EndTable();
}

void Engine::MyGUI::TextFloat(const char* label, float value, uint32_t precision) {

	DrawScalarTextField(label, value, precision);
}

void Engine::MyGUI::TextVector2(const char* label, const Vector2& value, uint32_t precision) {

	const float values[2] = { value.x, value.y };
	DrawTextFields(label, { 'X', 'Y', '\0', '\0' }, values, 2, precision);
}

void Engine::MyGUI::TextVector3(const char* label, const Vector3& value, uint32_t precision) {

	const float values[3] = { value.x, value.y, value.z };
	DrawTextFields(label, { 'X', 'Y', 'Z', '\0' }, values, 3, precision);
}

void Engine::MyGUI::TextQuaternion(const char* label, const Quaternion& value, uint32_t precision) {

	const float values[4] = { value.x, value.y, value.z, value.w };
	DrawTextFields(label, { 'X', 'Y', 'Z', 'W' }, values, 4, precision);
}

Engine::ValueEditResult Engine::MyGUI::DragInt(const char* label, int32_t& value, const IntEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label, setting.propertyRow)) {
		return result;
	}

	int32_t v = value;
	result.valueChanged = ImGui::DragInt("##Value", &v, setting.dragSpeed, setting.minValue, setting.maxValue);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit();

	if (result.valueChanged) {
		value = v;
	}

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::ColorEdit(
	const char* label, Color3& value,
	ImGuiColorEditFlags flags) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	float color[3] = { value.r, value.g, value.b };
	result.valueChanged =
		ImGui::ColorEdit3("##Value", color, flags);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit() || result.valueChanged;

	if (result.valueChanged) {
		value.r = color[0];
		value.g = color[1];
		value.b = color[2];
	}

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::ColorEdit(
	const char* label, Color4& value,
	ImGuiColorEditFlags flags) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	float color[4] = { value.r, value.g, value.b, value.a };
	result.valueChanged =
		ImGui::ColorEdit4("##Value", color, flags);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit() || result.valueChanged;

	if (result.valueChanged) {
		value.r = color[0];
		value.g = color[1];
		value.b = color[2];
		value.a = color[3];
	}

	EndPropertyRow();
	return result;
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate2D(const char* id,
	const GizmoViewContext& context, TransformComponent& transform) {

	GizmoEditResult result{};

	// ギズモ操作が有効で、かつ有効な描画領域がある場合にのみ操作を行う
	if (context.mode == SceneViewManipulatorMode::None || !context.rect.IsValid()) {
		return result;
	}

	// 操作モードをImGuizmoの形式に変換し無効なモードの場合は操作を行わない
	ImGuizmo::OPERATION operation = ToImGuizmoOperation(context.mode);
	if (operation == static_cast<ImGuizmo::OPERATION>(0)) {
		return result;
	}

	// 2DギズモはZ軸回転のみを扱うため、現在のローカル回転からZ軸回転を抽出して、他の回転成分を打ち消した行列を作る
	float currentRotationZ = ExtractRotationZDegrees2D(transform.localRotation);
	TransformComponent planeTransform = transform;
	planeTransform.localRotation = Quaternion::Normalize(Quaternion::FromEulerDegrees(Vector3(0.0f, 0.0f, currentRotationZ)));
	planeTransform.localScale.z = 1.0f;

	// ローカルSRTからワールド行列を計算する
	Matrix4x4 localMatrix = Matrix4x4::MakeAffineMatrix(planeTransform.localScale, planeTransform.localRotation, planeTransform.localPos);
	Matrix4x4 worldMatrix = localMatrix * context.parentWorldMatrix;

	// ImGuizmoは行列をfloat[16]の形式で受け取るので変換する
	float view[16]{};
	float projection[16]{};
	float matrix[16]{};
	Math::MatrixToFloat16(context.viewMatrix, view);
	Math::MatrixToFloat16(context.projectionMatrix, projection);
	Math::MatrixToFloat16(worldMatrix, matrix);

	// ギズモ操作を開始する
	BeginGizmoManipulate(id, context, true);

	result.valueChanged = ImGuizmo::Manipulate(view, projection, operation, ImGuizmo::LOCAL, matrix,
		nullptr, context.useSnap ? context.snapValues : nullptr);
	result.isOver = ImGuizmo::IsOver();
	result.isUsing = ImGuizmo::IsUsing();

	// ギズモ操作を終了する
	EndGizmoManipulate();

	// 値が変更されていない場合はこれ以上の処理は不要
	if (!result.valueChanged) {
		return result;
	}

	// 編集された行列をMatrix4x4に変換し、親のワールド行列の逆行列を掛けてローカル行列に変換する
	Matrix4x4 editedWorld = Math::MatrixFromFloat16(matrix);
	Matrix4x4 editedLocal = editedWorld * Matrix4x4::Inverse(context.parentWorldMatrix);

	Vector3 pos{};
	Vector3 scale{};
	float rotationZ = 0.0f;
	if (!DecomposeAffine2D(editedLocal, pos, rotationZ, scale)) {
		result.valueChanged = false;
		return result;
	}

	// 2DギズモはZ軸回転のみを扱うため、編集されたZ軸回転を現在のZ軸回転に連続させる
	float nextRotationZ = Math::MakeContinuousAngleDegrees(rotationZ, currentRotationZ);

	// 編集結果を反映
	transform.localPos.x = pos.x;
	transform.localPos.y = pos.y;
	transform.localPos.z = pos.z;
	transform.localRotation = Quaternion::Normalize(Quaternion::FromEulerDegrees(Vector3(0.0f, 0.0f, nextRotationZ)));
	transform.localScale.x = scale.x;
	transform.localScale.y = scale.y;
	transform.isDirty = true;
	return result;
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate3D(const char* id,
	const GizmoViewContext& context, TransformComponent& transform) {

	GizmoEditResult result{};

	// ギズモ操作が有効で、かつ有効な描画領域がある場合にのみ操作を行う
	if (context.mode == SceneViewManipulatorMode::None || !context.rect.IsValid()) {
		return result;
	}

	// 操作モードをImGuizmoの形式に変換し無効なモードの場合は操作を行わない
	ImGuizmo::OPERATION operation = ToImGuizmoOperation(context.mode);
	if (operation == static_cast<ImGuizmo::OPERATION>(0)) {
		return result;
	}

	// ローカルSRTからワールド行列を計算する
	Matrix4x4 localMatrix = Matrix4x4::MakeAffineMatrix(transform.localScale, transform.localRotation, transform.localPos);
	Matrix4x4 worldMatrix = localMatrix * context.parentWorldMatrix;

	// ImGuizmoは行列をfloat[16]の形式で受け取るので変換する
	float view[16]{};
	float projection[16]{};
	float matrix[16]{};
	Math::MatrixToFloat16(context.viewMatrix, view);
	Math::MatrixToFloat16(context.projectionMatrix, projection);
	Math::MatrixToFloat16(worldMatrix, matrix);

	// ギズモ操作を開始する
	BeginGizmoManipulate(id, context, false);

	result.valueChanged = ImGuizmo::Manipulate(view, projection, operation, ImGuizmo::LOCAL, matrix,
		nullptr, context.useSnap ? context.snapValues : nullptr);
	result.isOver = ImGuizmo::IsOver();
	result.isUsing = ImGuizmo::IsUsing();

	// ギズモ操作を終了する
	EndGizmoManipulate();

	// 値が変更されていない場合はこれ以上の処理は不要
	if (!result.valueChanged) {
		return result;
	}

	// 編集された行列をMatrix4x4に変換し、親のワールド行列の逆行列を掛けてローカル行列に変換する
	Matrix4x4 editedWorld = Math::MatrixFromFloat16(matrix);
	Matrix4x4 editedLocal = editedWorld * Matrix4x4::Inverse(context.parentWorldMatrix);

	Vector3 pos{};
	Vector3 scale{};
	Quaternion rotation{};
	if (!Engine::DecomposeAffine3D(editedLocal, pos, rotation, scale)) {
		result.valueChanged = false;
		return result;
	}
	// 編集結果を反映
	transform.localPos = pos;
	transform.localRotation = rotation;
	transform.localScale = scale;
	transform.isDirty = true;
	return result;
}

bool Engine::MyGUI::Checkbox(const char* label, bool& value, const PropertyRowSetting& setting) {

	if (!BeginPropertyRow(label, setting)) {
		return false;
	}

	bool changed = ImGui::Checkbox("##Value", &value);

	EndPropertyRow();
	return changed;
}

bool Engine::MyGUI::SmallCheckbox(const char* id, bool& value) {

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

	const bool changed = ImGui::Checkbox(id, &value);

	ImGui::PopStyleVar(2);
	return changed;
}

Engine::ValueEditResult Engine::MyGUI::InputText(const char* label, std::string& text, const TextEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label, setting.propertyRow)) {
		return result;
	}

	bool submittedByEnter = false;
	if (setting.multiLine) {

		ImVec2 inputSize = setting.size;
		if (inputSize.x <= 0.0f) {
			inputSize.x = ImGui::GetContentRegionAvail().x;
		}
		if (inputSize.y <= 0.0f) {
			inputSize.y = ImGui::GetFrameHeightWithSpacing() * 4.0f;
		}
		ImGui::InputTextMultiline("##Value", &text, inputSize, setting.flags);
	} else {

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		submittedByEnter = ImGui::InputText("##Value", &text, setting.flags | ImGuiInputTextFlags_EnterReturnsTrue);
	}

	result.valueChanged = ImGui::IsItemEdited();
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = submittedByEnter || ImGui::IsItemDeactivatedAfterEdit();

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::StringCombo(const char* label, std::string& currentValue,
	std::span<const std::string> items, const char* emptyPreview,
	bool allowEmptySelection, const ComboEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label, setting.propertyRow)) {
		return result;
	}

	const char* preview = currentValue.empty() ? emptyPreview : currentValue.c_str();
	if (items.empty()) {

		ImGui::TextDisabled("%s", emptyPreview);
	} else {

		const float width = ImGui::GetContentRegionAvail().x - setting.reserveRightWidth;
		ImGui::SetNextItemWidth((std::max)(1.0f, width));
	}
	if (!items.empty() && ImGui::BeginCombo("##Value", preview)) {
		// 空選択を許可する場合
		if (allowEmptySelection) {
			const bool selected = currentValue.empty();
			if (ImGui::Selectable(emptyPreview, selected)) {
				if (!currentValue.empty()) {
					currentValue.clear();
					result.valueChanged = true;
				}
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}

		int itemIndex = 0;
		for (const std::string& item : items) {

			ImGui::PushID(itemIndex);
			const bool selected = (currentValue == item);
			const char* itemLabel = item.empty() ? "##empty" : item.c_str();
			if (ImGui::Selectable(itemLabel, selected)) {
				if (currentValue != item) {
					currentValue = item;
					result.valueChanged = true;
				}
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
			++itemIndex;
		}
		ImGui::EndCombo();
	}

	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

	EndPropertyRow();
	return result;
}
