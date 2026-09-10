#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>

// c++
#include <array>
#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace Engine {

	// 実行中のスクリプトを世代付きハンドルで識別する
	struct ScriptProfileOwner {
		uint64_t id = 0;
		ManagedNativeEntity entity{};
		uint64_t slotID = 0;
		std::string typeID;
		std::string typeName;
	};

	struct ScriptProfileValue {
		double inclusiveMs = 0;
		double selfMs = 0;
		uint32_t calls = 0;
	};

	struct ScriptProfileRow {
		ScriptProfileOwner owner;
		std::string name;
		int32_t parent = -1;
		bool detail = false;
		bool grouped = false;
		ScriptProfileValue current{};
		std::array<ScriptProfileValue, 300> history{};
	};

	//============================================================================
	//	ScriptProfiler class
	//	コールバックと明示区間を上限付きで集計する
	//============================================================================
	class ScriptProfiler {
	public:
		static ScriptProfiler& GetInstance();
		static uint64_t OwnerID(ManagedScriptInstanceHandle handle);
		void Register(ScriptProfileOwner owner);
		void Unregister(ManagedScriptInstanceHandle handle);
		void ResetOwners();
		void Configure(bool enabled, std::string typeName, uint64_t ownerID);
		void Clear();
		void BeginFrame();
		void EndFrame();
		uint64_t BeginCallback(ManagedScriptInstanceHandle handle, const char* name);
		uint64_t BeginDetail(ManagedNativeEntity entity, uint64_t slotID, const char* name);
		void End(uint64_t token, bool detail);

		bool IsEnabled() const { return enabled_; }
		bool IsOverflowed() const { return overflowed_; }
		const std::string& DetailType() const { return detailType_; }
		uint64_t DetailOwner() const { return detailOwner_; }
		const auto& Owners() const { return owners_; }
		const auto& Rows() const { return rows_; }
		uint32_t FrameCount() const { return frameCount_; }
		uint32_t LastFrame() const { return (frameIndex_ + 299) % 300; }

	private:
		using Clock = std::chrono::steady_clock;
		using RowKey = std::tuple<bool, uint64_t, std::string, int32_t, std::string>;
		struct RowLess {
			using is_transparent = void;
			template <typename A, typename B>
			bool operator()(const A& a, const B& b) const {
				return std::tuple{ std::get<0>(a), std::get<1>(a), std::string_view(std::get<2>(a)),
					std::get<3>(a), std::string_view(std::get<4>(a)) } <
					std::tuple{ std::get<0>(b), std::get<1>(b), std::string_view(std::get<2>(b)),
					std::get<3>(b), std::string_view(std::get<4>(b)) };
			}
		};
		using EntityKey = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint64_t>;
		struct Sample {
			uint64_t token = 0;
			int32_t row = -1;
			int32_t instanceRow = -1;
			Clock::time_point start{};
			double childrenMs = 0;
		};

		// 既存の行を再利用し、上限を超えた区間は記録しない
		int32_t FindRow(const ScriptProfileOwner& owner, const char* name, bool detail, bool grouped, int32_t parent);
		// 記録可能な区間だけ時計を開始する
		uint64_t Begin(const ScriptProfileOwner& owner, const char* name, bool detail);
		// 選択対象をManaged側へ通知し、計測しない区間の境界呼び出しを省く
		void NotifySelection();

		std::map<uint64_t, ScriptProfileOwner> owners_;
		std::map<EntityKey, uint64_t> entityOwners_;
		std::map<RowKey, int32_t, RowLess> rowLookup_;
		std::vector<ScriptProfileRow> rows_;
		std::vector<Sample> callbackStack_;
		std::vector<Sample> detailStack_;
		std::string detailType_;
		uint64_t detailOwner_ = 0;
		uint64_t nextToken_ = 1;
		uint32_t frameIndex_ = 0;
		uint32_t frameCount_ = 0;
		bool enabled_ = false;
		bool overflowed_ = false;
	};

	// コールバックの早期終了でも区間を閉じる
	class ScriptProfileScope {
	public:
		ScriptProfileScope(ManagedScriptInstanceHandle handle, const char* name) :
			token_(ScriptProfiler::GetInstance().BeginCallback(handle, name)) {}
		~ScriptProfileScope() { ScriptProfiler::GetInstance().End(token_, false); }
		ScriptProfileScope(const ScriptProfileScope&) = delete;
		ScriptProfileScope& operator=(const ScriptProfileScope&) = delete;
	private:
		uint64_t token_ = 0;
	};
}
