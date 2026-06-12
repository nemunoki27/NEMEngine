#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ScriptCallbackKind enum
	//	detail profilerが計測するscript callbackの種別
	//============================================================================
	enum class ScriptCallbackKind : uint8_t {

		FixedUpdate,
		Update,
		LateUpdate,
		Count,
	};

	const char* ToString(ScriptCallbackKind kind);

	//============================================================================
	//	ManagedScriptProfileEntry struct
	//	typeIDとslotとcallbackの組に対する累積計測
	//============================================================================
	struct ManagedScriptProfileEntry {

		uint32_t typeID = 0;
		uint32_t entityIndex = 0; // owner Entity の index、selected entity filter 用
		int32_t slot = 0;
		ScriptCallbackKind callback = ScriptCallbackKind::Update;
		double totalMs = 0.0;
		double maxMs = 0.0;
		uint64_t callCount = 0;
		uint64_t exceptionCount = 0;
	};

	//============================================================================
	//	ManagedScriptProfilerStore class
	//	preallocatedかつboundedなscript callback detail store
	//============================================================================
	class ManagedScriptProfilerStore {
	public:
		//========================================================================
		//	policy / bounds
		//========================================================================

		// 詳細計測の有効無効、Releaseは_DEBUGと_DEVELOPBUILDいずれも未定義なので無効
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		static constexpr bool kDetailEnabled = true;
#else
		static constexpr bool kDetailEnabled = false;
#endif
		static constexpr size_t kMaxEntries = 512; // 保持する type と slot と callback 組の上限

		//========================================================================
		//	public Methods
		//========================================================================

		// 1 callbackの計測を加算する、detail無効時は即returnし呼び出し側もif constexprで除去する
		void Record(uint32_t typeID, uint32_t entityIndex, int32_t slot, ScriptCallbackKind callback,
			float milliseconds, bool threwException);

		// coroutine resumeの所要時間をaggregateで加算する、TickFrame周辺で計測する
		void RecordCoroutineResume(float milliseconds);

		// 全entryとoverheadを0に戻す、Play開始時や明示Reset時に呼ぶ
		void Reset();

		//--------- accessor -----------------------------------------------------

		const std::vector<ManagedScriptProfileEntry>& Entries() const { return entries_; }
		size_t EntryCount() const { return entries_.size(); }
		bool IsCapped() const { return capped_; }

		double CoroutineResumeMs() const { return coroutineResumeMs_; }
		// このstore自身が消費した計測overheadの累積
		double ProfilerOverheadMs() const { return overheadMs_; }

		uint64_t Version() const { return version_; }

		// シングルトン
		static ManagedScriptProfilerStore& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// packed keyをentries_のindexへ対応付ける、keyはtypeIDとentityIndexとslotとcallbackを畳んだ値
		std::unordered_map<uint64_t, size_t> keyToIndex_;
		std::vector<ManagedScriptProfileEntry> entries_;
		double coroutineResumeMs_ = 0.0;
		double overheadMs_ = 0.0;
		uint64_t version_ = 0;
		bool capped_ = false;
	};

	//============================================================================
	//	ScriptProfileSample class
	//	callback呼び出しを囲んで所要時間を計測するRAII
	//============================================================================
	class ScriptProfileSample {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptProfileSample(uint32_t typeID, uint32_t entityIndex, int32_t slot, ScriptCallbackKind callback) :
			typeID_(typeID), entityIndex_(entityIndex), slot_(slot), callback_(callback) {
			if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
				start_ = std::chrono::high_resolution_clock::now();
			}
		}
		~ScriptProfileSample() {
			if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
				const std::chrono::duration<float, std::milli> elapsed =
					std::chrono::high_resolution_clock::now() - start_;
				ManagedScriptProfilerStore::GetInstance().Record(typeID_, entityIndex_, slot_, callback_, elapsed.count(), faulted_);
			}
		}

		ScriptProfileSample(const ScriptProfileSample&) = delete;
		ScriptProfileSample& operator=(const ScriptProfileSample&) = delete;

		// callback後にfaultedを検知したら立てる、exception countに反映する
		void MarkFaulted() { faulted_ = true; }
	private:
		//--------- variables ----------------------------------------------------

		uint32_t typeID_;
		uint32_t entityIndex_;
		int32_t slot_;
		ScriptCallbackKind callback_;
		bool faulted_ = false;
		std::chrono::high_resolution_clock::time_point start_{};
	};
} // Engine
