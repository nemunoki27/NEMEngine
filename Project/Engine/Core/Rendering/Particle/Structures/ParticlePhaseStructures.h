#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Particle/ParticleValue.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialStructures.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticlePhaseStructures
	//	粒子の一生を区切るフェーズの定義、粒子はフェーズごとの寿命とモジュールで更新される
	//============================================================================
	// 寿命が尽きたときの挙動
	enum class ParticleLifeEndMode :
		uint8_t {

		Advance, // 次フェーズへ遷移する、最終フェーズなら破棄する
		Clamp,   // 進行度1.0の見た目を保持して生存し続ける
		Reset,   // 同フェーズを最初からやり直す
		Kill,    // 破棄する
	};

	// モジュール1つ分の定義、idでモジュールを引きparamsは各モジュールが解釈する
	struct ParticleEffectModuleEntry {

		// モジュールの識別子
		std::string id;
		// モジュールごとのパラメータ
		nlohmann::json params = nlohmann::json::object();
	};

	// フェーズ中の粒子を追従させる親設定
	struct ParticlePhaseParentSettings {

		// エミッターを親にするか
		bool useEmitter = false;
		// 任意の親エンティティのシーンローカルID
		UUID entityLocalFileID{};
		// 親の回転を無視するか
		bool ignoreParentRotation = false;
		// 親のスケールを無視するか
		bool ignoreParentScale = false;
		// 親を解除するときにワールド姿勢を維持するか
		bool keepWorldOnDetach = true;

		bool HasParent() const { return useEmitter || static_cast<bool>(entityLocalFileID); }
	};

	// 1フェーズ分の定義
	struct ParticleEffectPhase {

		// フェーズの名前
		std::string name = "Phase";
		// このフェーズでの粒子の寿命
		ParticleValue<float> lifetime{ 1.0f };
		// 寿命が尽きたときの挙動
		ParticleLifeEndMode lifeEndMode = ParticleLifeEndMode::Kill;
		// フェーズのマテリアル、未設定ならエフェクト共通のものを引き継ぐ
		AssetID material{};
		// フェーズ固有のマテリアル上書きと寿命アニメーション
		ParticlePhaseMaterialSettings materialSettings{};
		// 粒子を追従させる親設定
		ParticlePhaseParentSettings parentSettings{};
		// 使用されるモジュールのリスト
		std::vector<ParticleEffectModuleEntry> modules;
	};
} // Engine
