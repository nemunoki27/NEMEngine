#include "AudioHardwareTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Audio/AudioVoiceOwner.h>
#include <Engine/Core/Audio/AudioMediaReader.h>
#include <Engine/Core/World/Systems/Audio/AudioWorldVoiceStorage.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Editor/Assets/Preview/AudioPreviewSession.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>
#include <iostream>

namespace {

	void WaitForAudio() {

		std::this_thread::sleep_for(std::chrono::milliseconds(80));
	}

	bool TestOutputMatrix() {

		Engine::AudioDevice device;
		device.Init();
		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = 1;
		format.nSamplesPerSec = 44100;
		format.wBitsPerSample = 16;
		format.nBlockAlign = 2;
		format.nAvgBytesPerSec = 88200;
		bool passed = false;
		{
			IXAudio2SourceVoice* rawVoice = nullptr;
			HRESULT result = device.CreateSourceVoice(&rawVoice, &format);
			Engine::AudioVoiceOwner voice(rawVoice);
			if (FAILED(result) || !voice) { voice.reset(); device.Finalize(); return false; }
			voice->SetVolume(0.0f);
			auto normal = device.GetOutputMatrix(*voice, 1);
			Engine::AudioListenerState listener;
			listener.active = true;
			Engine::AudioSpatialState source;
			source.blend = 1.0f;
			source.position = { -1.0f, 0.0f, 1.0f };
			device.ApplySpatialMatrix(*voice, 1, normal, source, listener);
			auto left = device.GetOutputMatrix(*voice, 1);
			source.position.x = 1.0f;
			device.ApplySpatialMatrix(*voice, 1, normal, source, listener);
			auto right = device.GetOutputMatrix(*voice, 1);
			source.blend = 0.0f;
			device.ApplySpatialMatrix(*voice, 1, normal, source, listener);
			passed = device.GetOutputMatrix(*voice, 1) == normal;
			for (float value : left) { passed &= std::isfinite(value); }
			for (float value : right) { passed &= std::isfinite(value); }
			if (normal.size() == 2) { passed &= left[0] > left[1] && right[1] > right[0]; }
		}
		device.Finalize();
		return passed;
	}
}

bool NEMTests::TestAudioHardware() {

	HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(result)) { return false; }
	bool passed = TestOutputMatrix();
	auto check = [&](bool condition, const char* label) {
		if (!condition) { std::cerr << "Audio hardware: " << label << "\n"; }
		passed &= condition;
	};
	TestDirectory directory("AudioHardware");
	auto path = directory.GetPath() / L"再生位置.wav";
	// 無音のPCMを使い、実機Voiceの時計と停止理由を確認する
	WAVEFORMATEX format{};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 1;
	format.nSamplesPerSec = 44100;
	format.nAvgBytesPerSec = 88200;
	format.nBlockAlign = 2;
	format.wBitsPerSample = 16;
	uint32_t dataSize = 88200;
	uint32_t riffSize = dataSize + 36;
	uint32_t formatSize = 16;
	{
		std::ofstream file(path, std::ios::binary);
		file.write("RIFF", 4);
		file.write(reinterpret_cast<const char*>(&riffSize), 4);
		file.write("WAVEfmt ", 8);
		file.write(reinterpret_cast<const char*>(&formatSize), 4);
		file.write(reinterpret_cast<const char*>(&format), 16);
		file.write("data", 4);
		file.write(reinterpret_cast<const char*>(&dataSize), 4);
		std::vector<char> silence(dataSize);
		file.write(silence.data(), silence.size());
		passed &= static_cast<bool>(file);
	}
	Engine::Audio* audio = Engine::Audio::GetInstance();
	audio->Init();
	audio->SetMasterVolume(0.0f);
	passed &= audio->EnsureLoaded(path);
	std::string error;
	check(Engine::ReadAudioMedia(path, error).has_value() && error.empty(), "Media Foundation PCM decode");
	if (!error.empty()) { std::cerr << error << "\n"; }
	auto brokenPath = directory.GetPath() / "Broken.mp3";
	{
		std::ofstream file(brokenPath, std::ios::binary);
		file << "Invalid Audio";
	}
	passed &= !audio->EnsureLoaded(brokenPath);
	auto key = Engine::Algorithm::PathToUTF8(path);
	auto game = audio->PlayManaged(key, true);
	auto preview = audio->PlayManaged(key, true, 0.0f, Engine::AudioPlaybackOwner::EditorPreview);
	WaitForAudio();
	uint64_t playingPosition = audio->GetVoiceSamplePosition(game);
	passed &= game != 0 && preview != 0 && playingPosition > 0;
	audio->PauseVoice(game);
	WaitForAudio();
	uint64_t pausedPosition = audio->GetVoiceSamplePosition(game);
	audio->SetGamePauseReason(Engine::AudioPauseReason::Editor, true);
	audio->SetGamePauseReason(Engine::AudioPauseReason::Editor, false);
	WaitForAudio();
	passed &= pausedPosition == audio->GetVoiceSamplePosition(game) && !audio->IsVoicePlaying(game);
	audio->ResumeVoice(game);
	WaitForAudio();
	passed &= audio->GetVoiceSamplePosition(game) > pausedPosition;
	// 0ピッチとEditor pauseを別々に解除する
	audio->SetVoicePitch(game, 0.0f);
	WaitForAudio();
	uint64_t zeroPosition = audio->GetVoiceSamplePosition(game);
	audio->SetGamePauseReason(Engine::AudioPauseReason::Editor, true);
	audio->SetVoicePitch(game, 2.0f);
	WaitForAudio();
	passed &= audio->GetVoiceSamplePosition(game) == zeroPosition;
	audio->SetGamePauseReason(Engine::AudioPauseReason::Editor, false);
	WaitForAudio();
	passed &= audio->GetVoiceSamplePosition(game) > zeroPosition;
	audio->SetVoicePitch(game, -1.0f);
	WaitForAudio();
	passed &= audio->IsVoiceAlive(game);
	audio->SetVoiceLoop(game, false);
	audio->SetVoiceLoop(game, true);
	WaitForAudio();
	passed &= audio->IsVoiceAlive(game);
	audio->StopGameVoices();
	passed &= !audio->IsVoiceAlive(game) && audio->IsVoiceAlive(preview);
	{
		Engine::ECSWorld world;
		Engine::Entity entity = world.CreateEntity();
		world.AddComponent<Engine::UISelectableComponent>(entity);
		uint64_t uiVoice = audio->PlayManaged(key, true);
		auto& owned = world.GetStorage().Get<Engine::AudioWorldVoiceStorage>();
		owned.Add(entity, uiVoice);
		owned.Update(world);
		check(audio->IsVoiceAlive(uiVoice), "UI voice before deletion");
		world.DestroyEntity(entity);
		owned.Update(world);
		check(!audio->IsVoiceAlive(uiVoice), "UI voice after deletion");
	}
	{
		Engine::AudioPreviewSession first;
		Engine::AudioPreviewSession second;
		check(first.Play(path, true, 0.0f, 1.0f) && second.Play(path, true, 0.0f, 1.0f), "Independent previews");
		first.Stop();
		passed &= audio->IsVoiceAlive(preview);
	}
	Engine::Audio::Finalize();
	CoUninitialize();
	return passed;
}
