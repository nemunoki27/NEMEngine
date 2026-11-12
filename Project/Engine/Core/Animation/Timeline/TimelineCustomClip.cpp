#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Scripting/Managed/ManagedTimelineTypes.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <stdexcept>
#include <vector>

using namespace Engine;

namespace {

	// 独自クリップの実体を再生インスタンスごとに所有する
	class CustomClip final : public ITimelineClipInstance {
	public:

		explicit CustomClip(const TimelineClip& clip) : typeName_(clip.data.value("typeName", std::string{})),
			fields_(clip.data.value("fields", nlohmann::json::object())) {}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (typeName_.empty()) { diagnostic = "独自クリップの種類を設定してください"; return false; }
			const auto config = Configuration(context, fields_);
			if (!ManagedScriptRuntime::GetInstance().CreateTimelineClip(context.world, context.owner, context.systems,
				typeName_, config.dump(), handle_)) {
				diagnostic = "独自クリップを作成できません: " + typeName_;
				return false;
			}
			bindings_.reserve(context.bindings.size());
			parameters_.reserve(context.asset.parameters.size());
			return Invoke(context, 0);
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			const auto type = context.clip.data.find("typeName");
			if (context.output && type != context.clip.data.end() && type->is_string() && type->get_ref<const std::string&>() != typeName_)
				context.output->diagnostic = "独自クリップの種類変更は次の再生から適用されます";
			if (handle_ && !Invoke(context, 1)) throw std::runtime_error("独自クリップの評価に失敗しました: " + typeName_);
		}

		void Exit(TimelineEvaluationContext& context, TimelineExitReason reason) override {

			if (!handle_) return;
			ManagedScriptRuntime::GetInstance().ReleaseTimelineClip(handle_, context.systems, static_cast<int32_t>(reason));
			handle_ = 0;
		}

		bool RefreshConfiguration(TimelineEvaluationContext& context, const TimelineClip& clip, std::string& diagnostic) override {

			const std::string type = clip.data.value("typeName", std::string{});
			const bool deferred = type != typeName_;
			const auto fields = deferred ? fields_ : clip.data.value("fields", nlohmann::json::object());
			if (!ManagedScriptRuntime::GetInstance().UpdateTimelineClipConfiguration(handle_, context.systems,
				typeName_, Configuration(context, fields).dump())) {
				diagnostic = "独自クリップの設定を更新できません: " + typeName_;
				return false;
			}
			fields_ = fields;
			if (deferred && context.output) context.output->diagnostic = "独自クリップの種類変更は次の再生から適用されます";
			return true;
		}

	private:

		std::string typeName_;
		nlohmann::json fields_;
		uint64_t handle_ = 0;
		std::vector<ManagedTimelineBindingUpdate> bindings_;
		std::vector<ManagedTimelineParameterUpdate> parameters_;

		// 生成と編集更新で同じ参照定義を渡す
		static nlohmann::json Configuration(const TimelineEvaluationContext& context, const nlohmann::json& fields) {

			nlohmann::json config = {{"fields", fields}, {"timeline", ToAssetReferenceJson(context.asset.guid)},
				{"evaluationMode", static_cast<int32_t>(context.mode)},
				{"bindings", nlohmann::json::object()}, {"parameters", nlohmann::json::object()}};
			for (const auto& binding : context.bindings) {
				const auto native = MakeNativeEntity(context.world, binding.entity);
				config["bindings"][binding.name] = {{"worldIndex", native.world.index},
					{"worldGeneration", native.world.generation}, {"index", native.index}, {"generation", native.generation},
					{"name", binding.name}, {"id", ToString(binding.id)}};
				config["bindings"][ToString(binding.id)] = config["bindings"][binding.name];
			}
			for (const auto& parameter : context.asset.parameters) {
				nlohmann::json value;
				const auto* current = context.GetParameter(parameter.name);
				Engine::to_json(value, current ? *current : parameter.value);
				value["name"] = parameter.name;
				value["id"] = ToString(parameter.id);
				config["parameters"][parameter.name] = std::move(value);
				config["parameters"][ToString(parameter.id)] = config["parameters"][parameter.name];
			}
			return config;
		}

		// 型付きの再利用領域から現在の割り当てと値を渡す
		bool Invoke(const TimelineEvaluationContext& context, int32_t phase) {

			bindings_.clear();
			for (const auto& binding : context.bindings) {
				const auto native = MakeNativeEntity(context.world, binding.entity);
				bindings_.push_back({binding.id.value, native.world.index, native.world.generation, native.index, native.generation});
			}
			parameters_.clear();
			for (const auto& parameter : context.asset.parameters) {
				const auto* current = context.GetParameter(parameter.name);
				const auto& value = current ? *current : parameter.value;
				ManagedTimelineParameterUpdate update;
				update.id = parameter.id.value;
				update.value.kind = static_cast<int32_t>(value.index());
				if (const auto* boolean = std::get_if<bool>(&value)) update.value.integer = *boolean ? 1 : 0;
				else if (const auto* integer = std::get_if<int32_t>(&value)) update.value.integer = *integer;
				else if (const auto* scalar = std::get_if<float>(&value)) update.value.scalar = *scalar;
				else if (const auto* vector = std::get_if<Vector3>(&value)) {
					update.value.x = vector->x; update.value.y = vector->y; update.value.z = vector->z;
				} else update.stringValue = std::get<std::string>(value).c_str();
				parameters_.push_back(update);
			}
			const ManagedTimelineFrame frame{context.time, context.localTime, context.deltaTime, context.duration,
				context.weight, static_cast<int32_t>(context.mode), phase, 0, context.isScrubbing ? 1 : 0};
			return ManagedScriptRuntime::GetInstance().EvaluateTimelineClip(handle_, context.systems, frame,
				bindings_.data(), static_cast<int32_t>(bindings_.size()), parameters_.data(), static_cast<int32_t>(parameters_.size()));
		}
	};
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelineCustomClip([[maybe_unused]] const TimelineTrack& track,
	const TimelineClip& clip, [[maybe_unused]] std::string& diagnostic) {

	return std::make_unique<CustomClip>(clip);
}
