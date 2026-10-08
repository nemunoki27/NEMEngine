#include "ProfileCapture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
// c++
#include <cmath>
#include <string_view>

namespace {

	// 負値や非数値を処理時間として受け入れない
	bool IsProfileDuration(const nlohmann::json& value) {

		return value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= 0.0;
	}

	bool IsProfileGPUStatus(std::string_view status) {

		return status == "pending" || status == "complete" || status == "partial" || status == "missing" ||
			status == "readback_failed" || status == "capacity_exceeded";
	}

	// 自動終了と手動終了を読み込み後も区別する
	bool IsProfileStopReason(std::string_view reason) {

		return reason == "manual" || reason == "frame_limit" || reason == "capacity_exceeded" ||
			reason == "profiling_disabled" || reason == "application_exit";
	}

	// 比較画面が使用する各計測値の型を確認する
	bool IsProfileFrameValid(const nlohmann::json& frame) {

		if (!frame.contains("cpuMilliseconds") || !frame["cpuMilliseconds"].is_array()) { return false; }
		for (const auto& duration : frame["cpuMilliseconds"]) {
			if (!IsProfileDuration(duration)) { return false; }
		}
		for (const auto& pass : frame["gpuPasses"]) {
			if (!pass.is_object() || !pass.contains("name") || !pass["name"].is_string() ||
				!pass.contains("viewID") || !pass["viewID"].is_string() || !pass.contains("milliseconds") ||
				!IsProfileDuration(pass["milliseconds"])) { return false; }
		}
		if (!frame.contains("script")) { return true; }
		const auto& script = frame["script"];
		if (!script.is_object()) { return false; }
		if (script.contains("status") && (!script["status"].is_string() ||
			(script["status"] != "complete" && script["status"] != "partial" && script["status"] != "disabled"))) { return false; }
		if (!script.contains("rows")) { return !script.contains("status") || script["status"] == "disabled"; }
		if (!script["rows"].is_array()) { return false; }
		for (const auto& row : script["rows"]) {
			if (!row.is_object() || !row.contains("selfMilliseconds") || !IsProfileDuration(row["selfMilliseconds"]) ||
				!row.contains("inclusiveMilliseconds") || !IsProfileDuration(row["inclusiveMilliseconds"]) ||
				(row.contains("detail") && !row["detail"].is_boolean())) { return false; }
		}
		return true;
	}
}

Engine::ProfileCapture::ProfileCapture() = default;

Engine::ProfileCapture::~ProfileCapture() = default;

bool Engine::ProfileCapture::Start(uint32_t frameLimit) {

	if (recording_ || frameLimit == 0 || kMaxFrameCount < frameLimit) { return false; }
	// 前回の結果を新しい記録へ混ぜない
	snapshot_ = { { "schemaVersion", 1 }, { "frames", nlohmann::json::array() } };
	frameLimit_ = frameLimit;
	captureBytes_ = 0;
	recording_ = true;
	return true;
}

void Engine::ProfileCapture::Stop(std::string_view reason) {

	// 終了後の保存操作では元の終了理由を変えない
	if (!recording_) { return; }
	snapshot_["stopReason"] = reason;
	recording_ = false;
}

void Engine::ProfileCapture::Append(uint64_t frameID, nlohmann::json frame) {

	if (!recording_) { return; }
	// GPU結果は対応するフレームが戻るまで未取得として残す
	frame["frameID"] = frameID;
	frame["gpuStatus"] = "pending";
	frame["gpuPasses"] = nlohmann::json::array();
	// 設定やCameraが多い記録も上限内に収める
	const size_t frameBytes = frame.dump().size();
	if (kMaxCaptureBytes - captureBytes_ < frameBytes) {
		Stop("capacity_exceeded");
		return;
	}
	captureBytes_ += frameBytes;
	snapshot_["frames"].push_back(std::move(frame));
	if (snapshot_["frames"].size() >= frameLimit_) { Stop("frame_limit"); }
}

void Engine::ProfileCapture::AttachGPU(uint64_t frameID, const nlohmann::json& passes, std::string_view status) {

	if (!snapshot_.is_object()) { return; }
	// 遅れて取得したGPU結果も元のCPUフレームへ対応付ける
	for (auto iterator = snapshot_["frames"].rbegin(); iterator != snapshot_["frames"].rend(); ++iterator) {
		if ((*iterator)["frameID"] == frameID) {
			const size_t passBytes = passes.dump().size();
			const size_t previousBytes = (*iterator)["gpuPasses"].dump().size();
			const size_t additionalBytes = passBytes > previousBytes ? passBytes - previousBytes : 0;
			if (kMaxCaptureBytes - captureBytes_ < additionalBytes) {
				(*iterator)["gpuStatus"] = "capacity_exceeded";
				return;
			}
			captureBytes_ = captureBytes_ - previousBytes + passBytes;
			(*iterator)["gpuPasses"] = passes;
			(*iterator)["gpuStatus"] = status;
			return;
		}
	}
}

bool Engine::ProfileCapture::Save(const std::filesystem::path& path) const {

	if (!snapshot_.is_object() || recording_) { return false; }
	// 保存には現在の確定結果のコピーを渡す
	const nlohmann::json snapshot = snapshot_;
	return JsonAdapter::SaveCanonical(path, snapshot);
}

bool Engine::ProfileCapture::Load(const std::filesystem::path& path, nlohmann::json& result) {

	// 過大な記録は解析前に拒否する
	std::error_code error;
	const uintmax_t size = std::filesystem::file_size(path, error);
	if (error || size > 256ull * 1024 * 1024) { return false; }
	const nlohmann::json snapshot = JsonAdapter::Load(path, false);
	if (!snapshot.is_object() || !snapshot.contains("schemaVersion") || snapshot["schemaVersion"] != 1 ||
		!snapshot.contains("frames") || !snapshot["frames"].is_array() || snapshot["frames"].size() > kMaxFrameCount) {
		return false;
	}
	if (snapshot.contains("stopReason") && (!snapshot["stopReason"].is_string() ||
		!IsProfileStopReason(snapshot["stopReason"].get_ref<const std::string&>()))) { return false; }
	uint64_t previousID = 0;
	for (const auto& frame : snapshot["frames"]) {
		if (!frame.is_object() || !frame.contains("frameID") || !frame["frameID"].is_number_unsigned() ||
			!frame.contains("conditions") || !frame["conditions"].is_object() ||
			!frame.contains("gpuPasses") || !frame["gpuPasses"].is_array() ||
			!frame.contains("gpuStatus") || !frame["gpuStatus"].is_string() ||
			!IsProfileGPUStatus(frame["gpuStatus"].get_ref<const std::string&>()) || !IsProfileFrameValid(frame)) { return false; }
		const uint64_t frameID = frame["frameID"].get<uint64_t>();
		if (frameID <= previousID) { return false; }
		previousID = frameID;
	}
	result = snapshot;
	return true;
}

bool Engine::ProfileCapture::HasSameConditions(const nlohmann::json& lhs, const nlohmann::json& rhs) {

	if (!lhs.contains("frames") || !rhs.contains("frames") || !lhs["frames"].is_array() ||
		!rhs["frames"].is_array() || lhs["frames"].empty() || rhs["frames"].empty()) { return false; }
	if (!lhs["frames"].front().contains("conditions")) { return false; }
	const auto& conditions = lhs["frames"].front()["conditions"];
	// 収集に失敗した入力や不明なDriverを同条件と扱わない
	if ((conditions.contains("inputSnapshotComplete") && conditions["inputSnapshotComplete"] != true) ||
		(conditions.contains("driverVersion") && conditions["driverVersion"] == "unavailable")) { return false; }
	// 記録途中の設定変更も比較条件の不一致として扱う
	for (const auto* frames : { &lhs["frames"], &rhs["frames"] }) {
		for (const auto& frame : *frames) {
			if (!frame.contains("conditions") || frame["conditions"] != conditions) { return false; }
		}
	}
	return true;
}
