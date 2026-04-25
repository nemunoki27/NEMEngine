#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ToolTypes.h>

namespace Engine {

	//============================================================================
	//	ITool class
	//	エンジン/ゲーム共通のツールインターフェース
	//============================================================================
	class ITool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ITool() = default;
		virtual ~ITool() = default;

		// UI表示とは独立した更新処理。必要なツールだけ実装する
		virtual void Tick(ToolContext& context);

		// レジストリへ登録/解除されたときのフック
		virtual void OnRegistered();
		virtual void OnUnregistered();

		//--------- accessor -----------------------------------------------------

		// ツール情報の取得
		virtual const ToolDescriptor& GetDescriptor() const = 0;
		// ツールが現在の状態で使用可能か
		virtual bool IsEnabled(const ToolContext& context) const;
	};
} // Engine
