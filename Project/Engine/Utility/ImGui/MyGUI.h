#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Editor/EditorState.h>

// c++
#include <initializer_list>
#include <span>
#include <string>
// imgui
#include <imgui.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h> 

namespace Engine {

	// front
	class AssetDatabase;
	struct TransformComponent;
	struct MeshSubMeshTextureOverride;

	//============================================================================
	//	MyGUI structures
	//============================================================================

	// 値編集設定
	struct FloatEditSetting {

		float dragSpeed = 0.01f;    // 編集速度
		float minValue = -10000.0f; // 最小値
		float maxValue = 10000.0f;  // 最大値

		// プロパティを閉じるか
		bool closeOnProperty = true;
		// 右側に別UIを置くために残す幅
		float reserveRightWidth = 0.0f;
	};
	struct IntEditSetting {

		float dragSpeed = 1.0f;
		int32_t minValue = (std::numeric_limits<int32_t>::min)();
		int32_t maxValue = (std::numeric_limits<int32_t>::max)();
	};
	// 値編集の結果
	struct ValueEditResult {

		bool valueChanged = false;
		bool anyItemActive = false;
		bool editFinished = false;
	};
	// 文字入力ポップアップの操作結果
	struct TextInputPopupResult {

		bool submitted = false;
		bool canceled = false;
	};
	// 文字入力設定
	struct TextEditSetting {

		// 複数行入力にするか
		bool multiLine = false;
		// 複数行入力時のサイズ。0以下の場合は既定サイズを使う
		ImVec2 size = ImVec2(0.0f, 0.0f);
		// ImGuiの文字入力フラグ
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
	};
	// ビューポートの位置とサイズを表す構造体
	struct GizmoViewportRect {

		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;

		// ビューの幅と高さが1以上なら有効とみなす
		bool IsValid() const { return 1.0f <= width && 1.0f <= height; }
	};
	// ギズモの描画と操作に必要な情報をまとめた構造体
	struct GizmoViewContext {

		GizmoViewportRect rect{};

		// ビュー行列と射影行列
		Matrix4x4 viewMatrix = Matrix4x4::Identity();
		Matrix4x4 projectionMatrix = Matrix4x4::Identity();

		// ローカルからワールドに上げるための親行列
		Matrix4x4 parentWorldMatrix = Matrix4x4::Identity();

		// ギズモの操作モード
		SceneViewManipulatorMode mode = SceneViewManipulatorMode::None;

		// ギズモの操作空間
		bool orthographic = false;
		bool allowAxisFlip = true;
	};
	// ギズモの編集結果
	struct GizmoEditResult {

		bool valueChanged = false;
		bool isOver = false;
		bool isUsing = false;

		bool IsUse() const { return isOver || isUsing; }
	};

	//============================================================================
	//	MyGUI class
	//	エディター表示に使用するImGui関連のユーティリティクラス
	//============================================================================
	class MyGUI {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MyGUI() = default;
		~MyGUI() = default;

		//========================================================================
		//	汎用表示
		//========================================================================

		// エンジンの表示スタイルでコラプシングヘッダーを表示する
		static bool CollapsingHeader(const char* label, bool stratOpen = true);
		// ポップアップ内で使用する文字入力とOK/Cancelを描画する
		static TextInputPopupResult InputTextPopupContent(const char* label, std::string& text, const char* errorText = nullptr);
		// 汎用プロパティ行
		static bool BeginPropertyRow(const char* label);
		static void EndPropertyRow();

		//========================================================================
		//	数学関連
		//========================================================================

		// 表示
		static void TextFloat(const char* label, float value, uint32_t precision = 3);
		static void TextVector2(const char* label, const Vector2& value, uint32_t precision = 3);
		static void TextVector3(const char* label, const Vector3& value, uint32_t precision = 3);
		static void TextQuaternion(const char* label, const Quaternion& value, uint32_t precision = 3);
		static void TextMatrix4x4(const char* label, const Matrix4x4& value, uint32_t precision = 3);
		// ドラッグ編集
		static ValueEditResult DragFloat(const char* label, float& value, const FloatEditSetting& setting = FloatEditSetting{});
		static ValueEditResult DragInt(const char* label, int32_t& value, const IntEditSetting& setting = IntEditSetting{});
		static ValueEditResult DragVector2(const char* label, Vector2& value, const FloatEditSetting& setting = FloatEditSetting{});
		static ValueEditResult DragVector3(const char* label, Vector3& value, const FloatEditSetting& setting = FloatEditSetting{});
		static ValueEditResult DragVector4(const char* label, Vector4& value, const FloatEditSetting& setting = FloatEditSetting{});
		static ValueEditResult DragQuaternion(const char* label, Quaternion& value, bool displayEuler = false, const FloatEditSetting& setting = FloatEditSetting{});
		// 色編集
		static ValueEditResult ColorEdit(const char* label, Color3& value);
		static ValueEditResult ColorEdit(const char* label, Color4& value);

		//========================================================================
		//	ギズモ操作
		//========================================================================

		// 2D
		static GizmoEditResult Manipulate2D(const char* id, const GizmoViewContext& context, TransformComponent& transform);
		static GizmoEditResult Manipulate3D(const char* id, const GizmoViewContext& context, TransformComponent& transform);
		// 3D
		static GizmoEditResult Manipulate2D(const char* id, const GizmoViewContext& context, MeshSubMeshTextureOverride& subMesh);
		static GizmoEditResult Manipulate3D(const char* id, const GizmoViewContext& context, MeshSubMeshTextureOverride& subMesh);

		//========================================================================
		//	パラメータ変更
		//========================================================================

		// チェックボックス切り替え
		static bool Checkbox(const char* label, bool& value);
		static bool SmallCheckbox(const char* id, bool& value);
		// 入力
		static ValueEditResult InputText(const char* label, std::string& text, const TextEditSetting& setting = TextEditSetting{});
		// std::stringのコンボボックス
		static ValueEditResult StringCombo(const char* label, std::string& currentValue,
			std::span<const std::string> items, const char* emptyPreview = "<None>", bool allowEmptySelection = false);

		//========================================================================
		//	アセット参照
		//========================================================================

		// アセットID編集フィールドを描画する
		static ValueEditResult AssetReferenceField(const char* label,
			AssetID& value, const AssetDatabase* assetDatabase, const std::initializer_list<AssetType>& acceptedTypes = {});
	};
} // Engine
