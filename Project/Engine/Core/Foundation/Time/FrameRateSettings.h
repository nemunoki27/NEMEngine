#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	FrameRateSettings class
	// フレームレート上限を保持し設定ファイルへ永続化するシングルトン
	//============================================================================
	class FrameRateSettings {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FrameRateSettings() = default;
		~FrameRateSettings() = default;

		// 設定ファイルパスを記憶して値を読み込む、ファイルが無ければ既定のまま
		void Load(const std::string& configPath);
		// 現在の設定を記憶済みのパスへ保存する
		void Save() const;

		//--------- accessor -----------------------------------------------------

		// 目標フレームレートを設定する、0は制限なし
		void SetTargetFps(uint32_t fps) { targetFps_ = fps; }
		// 目標フレームレートを取得する、0は制限なし
		uint32_t GetTargetFps() const { return targetFps_; }
		// エディターの目標フレームレートを設定する、0は制限なし
		void SetEditorTargetFps(uint32_t fps) { editorTargetFps_ = fps; }
		// エディターの目標フレームレートを取得する、0は制限なし
		uint32_t GetEditorTargetFps() const { return editorTargetFps_; }
		// エディター用のフレームレート上限を使用するか設定する
		void SetUseEditorTargetFps(bool useEditor) { useEditorTargetFps_ = useEditor; }
		// Presentで使用する現在のフレームレート上限を取得する
		uint32_t GetPresentTargetFps() const {
			return useEditorTargetFps_ ? editorTargetFps_ : targetFps_;
		}

		// シングルトン
		static FrameRateSettings& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 目標フレームレートで0は制限なし、既定は60
		uint32_t targetFps_ = 60;
		// 編集中のGPU過負荷を防ぐエディター用上限
		uint32_t editorTargetFps_ = 60;
		// NEMEditorのみエディター用上限へ切り替える
		bool useEditorTargetFps_ = false;
		// 保存先の設定ファイルパス
		std::string configPath_{};
	};
} // Engine
