#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/Core/ITool.h>

// c++
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ToolRegistry class
	//	エンジンとゲームのツールを管理する
	//============================================================================
	class ToolRegistry {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ToolRegistry() = default;
		~ToolRegistry();
		ToolRegistry(const ToolRegistry&) = delete;
		ToolRegistry& operator=(const ToolRegistry&) = delete;

		// ツールを登録
		bool Register(std::unique_ptr<ITool> tool);
		// ツールを解除
		bool Unregister(std::string_view id);
		// 全ツールを解除
		void Clear();
		// 終了通知の失敗を診断して解除を続ける
		void ClearNoThrow() noexcept;

		// 毎フレーム更新
		void Tick(ToolContext& context);

		//--------- accessor -----------------------------------------------------

		ITool* Find(std::string_view id);
		const ITool* Find(std::string_view id) const;
		std::vector<ITool*> GetTools();
		std::vector<const ITool*> GetTools() const;
		// コールバック中も対象の寿命を保持する
		std::shared_ptr<ITool> Acquire(std::string_view id);
		// 走査開始時の順序と対象の寿命を保持する
		std::vector<std::shared_ptr<ITool>> GetToolSnapshot();
		bool Empty() const { return tools_.empty(); }

		// シングルトン
		static ToolRegistry& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::vector<std::shared_ptr<ITool>> tools_;
		std::unordered_map<std::string, uint32_t> idToIndex_;
		// 終了通知中の新規登録を拒否する
		bool clearing_ = false;

		//--------- functions ----------------------------------------------------

		// ID検索用のインデックスを作り直す
		void RebuildIndex();
		// 表示順が安定するように並び替える
		void SortTools();
	};

#define ENGINE_REGISTER_TOOL(T) \
	inline const bool kRegisteredTool_##T = Engine::ToolRegistry::GetInstance().Register(std::make_unique<T>());
}

