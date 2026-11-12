#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <span>

namespace Engine {

	struct EditorPanelContext;

	enum class InspectorFieldID {

		Enabled, Visible, DisplayLayer, CameraLayer, ReceiverLayer, ParticleGroup,
		SortLayer, DrawOrder, BlendMode, RenderQueue, PlayOnStart, PlayInEditMode,
		EditorPreview, UnscaledTime, Loop, PlaybackSpeed, Restitution, Friction,
		AllowTopple, AlphaThreshold, PlaybackMode, Duration, StartDelay, PlaybackState, AxisAngle, Once, PingPong, DrawDebug
	};

	enum class InspectorPlaybackAction { None, Play, Pause, Resume, Stop, Reset, EmitOnce, Clear };

	// 操作の意味は呼び出し側で適用し、並びと幅だけを共通化する
	InspectorPlaybackAction DrawInspectorPlaybackControls(bool enabled, bool timer = false,
		std::span<const InspectorPlaybackAction> actions = {});
	const char* GetInspectorPlaybackState(bool playing, bool paused, bool finished = false, bool timer = false);
	// 往復再生を持たないコンポーネントの繰り返しを編集する
	ValueEditResult DrawInspectorLoopField(bool& loop);

	enum class InspectorMaskDomain { Rendering, Particle, Collision };

	// 共通項目だけをドラフトへ接続する
	struct InspectorCommonFields {

		bool* enabled = nullptr;
		bool* visible = nullptr;
		uint32_t* displayLayer = nullptr;
		int32_t* cameraLayer = nullptr;
		uint32_t* receiverLayer = nullptr;
		uint32_t* particleGroup = nullptr;
		bool* drawDebug = nullptr;
	};

	// 表示名と内部識別子を分けて取得する
	const char* GetInspectorLabel(InspectorFieldID field);
	const char* GetInspectorFieldKey(InspectorFieldID field);
	// 用途ごとのビット集合を同じ操作で編集する
	ValueEditResult DrawInspectorMask(const EditorPanelContext& context, const char* label,
		uint32_t& value, InspectorMaskDomain domain, const char* id = nullptr);
	// 有効、表示、レイヤーの順で共通項目を描画する
	ValueEditResult DrawInspectorCommonFields(const EditorPanelContext& context, const InspectorCommonFields& fields);
	// コンポーネントの共通末尾項目を描画する
	ValueEditResult DrawInspectorCommonFooter(const InspectorCommonFields& fields);
	// 分類の表示名と順序を取得する
	const char* GetInspectorCategoryLabel(std::string_view category);
	int GetInspectorCategoryOrder(std::string_view category);
	std::string_view GetInspectorComponentCategory(std::string_view component, std::string_view fallback);

	// 入れ子のヘッダーと項目を同じ段下げで表示する
	class InspectorIndentScope {
	public:
		explicit InspectorIndentScope(const char* id = nullptr);
		~InspectorIndentScope();
		InspectorIndentScope(const InspectorIndentScope&) = delete;
		InspectorIndentScope& operator=(const InspectorIndentScope&) = delete;
	private:
		bool hasID_;
	};
}
