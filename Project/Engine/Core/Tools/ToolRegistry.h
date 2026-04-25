#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ITool.h>

// c++
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ToolRegistry class
	//	Engine/Game両方から追加されるツールを管理する
	//============================================================================
	class ToolRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ToolRegistry() = default;
		~ToolRegistry();

		// ツールを登録
		bool Register(std::unique_ptr<ITool> tool);
		// ツールを解除
		bool Unregister(std::string_view id);
		// 全ツールを解除
		void Clear();

		// 毎フレーム更新
		void Tick(ToolContext& context);

		//--------- accessor -----------------------------------------------------

		ITool* Find(std::string_view id);
		const ITool* Find(std::string_view id) const;
		std::vector<ITool*> GetTools();
		std::vector<const ITool*> GetTools() const;
		bool Empty() const { return tools_.empty(); }

		// シングルトン
		static ToolRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<std::unique_ptr<ITool>> tools_;
		std::unordered_map<std::string, uint32_t> idToIndex_;

		//--------- functions ----------------------------------------------------

		// ID検索用のインデックスを作り直す
		void RebuildIndex();
		// 表示順が安定するように並び替える
		void SortTools();
	};

#define ENGINE_REGISTER_TOOL(T) \
	namespace { \
		const bool kRegisteredTool_##T = Engine::ToolRegistry::GetInstance().Register(std::make_unique<T>()); \
	}
} // Engine
