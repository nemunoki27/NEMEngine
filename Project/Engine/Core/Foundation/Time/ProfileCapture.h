#pragma once

//============================================================================
//	include
//============================================================================
#include <json.hpp>

// c++
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Engine {

	//============================================================================
	//	ProfileCapture class
	//	描画条件とフレーム別の確定結果を保持する
	//============================================================================
	class ProfileCapture {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProfileCapture();
		~ProfileCapture();

		// 上限を指定して新しい記録を開始する
		bool Start(uint32_t frameLimit);
		// GPU待機を追加せず記録を終了する
		void Stop(std::string_view reason = "manual");
		// CPUの確定結果を追加する
		void Append(uint64_t frameID, nlohmann::json frame);
		// 対応するフレームへGPUの確定結果を追加する
		void AttachGPU(uint64_t frameID, const nlohmann::json& passes, std::string_view status);
		// 現在の確定結果をファイルへ保存する
		bool Save(const std::filesystem::path& path) const;
		// 保存済みの記録を検証して読み込む
		static bool Load(const std::filesystem::path& path, nlohmann::json& result);
		// 描画条件が一致する記録か確認する
		static bool HasSameConditions(const nlohmann::json& lhs, const nlohmann::json& rhs);

		//--------- accessor -----------------------------------------------------

		bool IsRecording() const { return recording_; }
		const nlohmann::json& GetSnapshot() const { return snapshot_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		static constexpr uint32_t kMaxFrameCount = 36000;
		static constexpr size_t kMaxCaptureBytes = 64ull * 1024 * 1024;
		size_t captureBytes_ = 0;
		uint32_t frameLimit_ = 0;
		bool recording_ = false;
		nlohmann::json snapshot_;
	};
}
