#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Debug/SpdLogger.h>
#include <Engine/Object/Base/KeyframeObject3D.h>
#include <Engine/Utility/Algorithm/Algorithm.h>
#include <Engine/Object/Data/Transform/Transform.h>
#include <Engine/Utility/Timer/StateTimer.h>

// imgui
#include <imgui.h>

namespace SakuEngine {

	//============================================================================
	//	BaseCamera class
	//	カメラの基底クラス、継承しなくてもこのクラスを使ってカメラを実装できる
	//============================================================================
	class BaseCamera {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 更新のモード
		enum class UpdateMode {

			Euler,
			Quaternion
		};
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BaseCamera();
		virtual ~BaseCamera() = default;

		virtual void Init() {};

		virtual void Update() {};
		// ビュープロジェクション行列の更新
		void UpdateView(UpdateMode updateMode = UpdateMode::Euler);

		// 共通エディター
		virtual void ImGui();
		// フラスタムの編集
		void EditFrustum();
		void RenderFrustum();

		// 自動フォーカス開始
		void StartAutoFocus(bool isFocus, const SakuEngine::Vector3& target);

		// カメラエディターの呼び出し
		void StartCameraAnim(const std::string& animName, bool isAddFirstKey = true,
			bool isUpdateKey = true, const std::optional<KeyframeInverseSetting>& inverseSetting = std::nullopt);
		void EndCameraAnim();
		// 親の設定、特定のキーにのみ設定
		void SetEditorParentTransform(const std::string& keyName, const SakuEngine::Transform3D& parent);

		// エディター更新が終了した瞬間のカメラ姿勢を取得
		void BindEndEditCameraPose();

		//--------- accessor -----------------------------------------------------

		void SetParent(const SakuEngine::Transform3D* parent) { transform_.parent = parent; };
		void SetTranslation(const SakuEngine::Vector3& translation) { transform_.translation = translation; }
		void SetRotation(const Quaternion& rotation) { transform_.rotation = rotation; }
		void SetEulerRotation(const SakuEngine::Vector3& eulerRotation) { transform_.eulerRotate = eulerRotation; }
		void SetFovY(float fovY) { fovY_ = fovY; }
		void SetIsUpdateEditor(bool isUpdateEditor) { isUpdateEditor_ = isUpdateEditor; }

		float GetFovY() const { return fovY_; }
		float GetNearClip() const { return nearClip_; }
		float GetFarClip() const { return farClip_; }
		bool IsUpdateDebugView() const { return updateDebugView_; }
		bool IsUpdateEditor() const { return isUpdateEditor_; }

		const SakuEngine::Transform3D& GetTransform() const { return transform_; }

		const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
		const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
		const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
		const Matrix4x4& GetBillboardMatrix() const { return billboardMatrix_; }

		// エディター更新が終了した瞬間のカメラ姿勢を取得
		const SakuEngine::Vector3& GetEndEditTranslation() const { return endEditTranslation_; }
		const Quaternion& GetEndEditRotation() const { return endEditRotation_; }
		float GetEndEditFovY() const { return endEditFovY_; }
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const float itemWidth_ = 224.0f;

		float fovY_;
		float nearClip_;
		float farClip_;
		float aspectRatio_;

		SakuEngine::Transform3D transform_;

		Matrix4x4 viewMatrix_;
		Matrix4x4 projectionMatrix_;
		Matrix4x4 viewProjectionMatrix_;
		Matrix4x4 billboardMatrix_;

		// 選択したオブジェクトへの自動フォーカス
		bool isStartFocus_ = false;
		StateTimer autoFucusTimer_;
		Vector3 startFocusTranslation_;
		Vector3 targetFocusTranslation_;
		Quaternion startFocusRotation_;
		Quaternion targetFocusRotation_;

		// エディター更新が終了した瞬間のカメラ姿勢
		Vector3 endEditTranslation_;
		Quaternion endEditRotation_;
		float endEditFovY_;

		// debug
		float frustumScale_;
		bool displayFrustum_;
		bool updateDebugView_;
		bool isUpdateEditor_ = false;

		//--------- functions ----------------------------------------------------

		// 自動フォーカス更新
		void UpdateAutoFocus();

		// ビルボード行列計算
		void CalBillboardMatrix();
	};

	//============================================================================
	//	BaseCamera log
	//============================================================================

	namespace CameraLog {

		// ログ出力
		void Output(const std::string& msg);
	}
}; // SakuEngine