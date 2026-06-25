#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <cstdint>

//============================================================================
//	InputStructures class
//============================================================================
// XINPUTGamePadのボタンの種類
enum class GamePadButtons {

	ARROW_UP,       // 十字ボタンの上方向
	ARROW_DOWN,     // 十字ボタンの下方向
	ARROW_LEFT,     // 十字ボタンの左方向
	ARROW_RIGHT,    // 十字ボタンの右方向
	START,          // スタートボタン
	BACK,           // バックボタン
	LEFT_THUMB,     // 左スティックのボタン
	RIGHT_THUMB,    // 右スティックのボタン
	LEFT_SHOULDER,  // 左ショルダーボタンLB
	RIGHT_SHOULDER, // 右ショルダーボタンRB
	LEFT_TRIGGER,   // 左トリガーLT
	RIGHT_TRIGGER,  // 右トリガーRT
	A,              // Aボタン
	B,              // Bボタン
	X,              // Xボタン
	Y,              // Yボタン
	Counts          // ボタンの数を表すための定数
};

// DIKコードの種類
enum class KeyDIKCode :
	uint8_t {

	ESCAPE = 0x01,

	_1 = 0x02,
	_2 = 0x03,
	_3 = 0x04,
	_4 = 0x05,
	_5 = 0x06,
	_6 = 0x07,
	_7 = 0x08,
	_8 = 0x09,
	_9 = 0x0A,
	_0 = 0x0B,

	MINUS = 0x0C,
	EQUALS = 0x0D,
	BACK = 0x0E,
	TAB = 0x0F,

	Q = 0x10,
	W = 0x11,
	E = 0x12,
	R = 0x13,
	T = 0x14,
	Y = 0x15,
	U = 0x16,
	I = 0x17,
	O = 0x18,
	P = 0x19,

	LBRACKET = 0x1A,
	RBRACKET = 0x1B,
	RETURN = 0x1C,
	LCONTROL = 0x1D,

	A = 0x1E,
	S = 0x1F,
	D = 0x20,
	F = 0x21,
	G = 0x22,
	H = 0x23,
	J = 0x24,
	K = 0x25,
	L = 0x26,

	SPACE = 0x39,
};

// マウス入力の種類
enum class MouseButton {

	Right,
	Left,
	Center
};

// 入力されている状態
enum class InputType {

	Keyboard,
	GamePad,
	Count
};

// 入力タイプ自動更新の検知トリガ、指定デバイスのボタン/キーまたはアナログ移動で切り替える
struct InputDetectTrigger {

	// 切り替え先のデバイス種別
	InputType device = InputType::Keyboard;
	// trueならスティック/マウス移動量で判定、falseならcodeのボタン/キーで判定
	bool isMovement = false;
	// isMovement=falseのときの判定コード、GamePadはGamePadButtons、KeyboardはDIKキー
	int32_t code = 0;
};

// 入力検知位置
enum class InputViewArea {

	Game,
	Scene,
	AnimationClip,
	InspectorModelPreview,
};

// 入力振動パラメータ
struct InputVibrationParams {

	float left;     // 0..1の低周波で重い
	float right;    // 0..1の高周波で軽い
	float duration; // 秒
	float attack;   // フェードイン秒
	float release;  // フェードアウト秒
	int priority;   // 予約

	// エディター
	void ImGui(const char* label);

	// json
	void FromJson(const nlohmann::json& data);
	void ToJson(nlohmann::json& data);
};
