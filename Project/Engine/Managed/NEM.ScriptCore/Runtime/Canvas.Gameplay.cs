using System;
using System.Collections.Generic;

namespace NEMEngine;

public enum CanvasInputAction {

    Up,
    Down,
    Left,
    Right,
    Submit
}

public sealed unsafe partial class Canvas {

    private const int KeyboardInputDevice = 0;
    private const int GamepadInputDevice = 1;

    // 指定操作に割り当てたキーボード入力を取得する
    public IReadOnlyList<KeyCode> GetKeyboardInputs(CanvasInputAction action) {

        int[] codes = NativeApi.CanvasGetInputBindings(
            entity.native, (int)action, KeyboardInputDevice);
        KeyCode[] bindings = new KeyCode[codes.Length];
        for (int i = 0; i < codes.Length; ++i) {
            bindings[i] = (KeyCode)codes[i];
        }
        return bindings;
    }

    // 指定操作に割り当てたゲームパッド入力を取得する
    public IReadOnlyList<GamepadButton> GetGamepadInputs(CanvasInputAction action) {

        int[] codes = NativeApi.CanvasGetInputBindings(
            entity.native, (int)action, GamepadInputDevice);
        GamepadButton[] bindings = new GamepadButton[codes.Length];
        for (int i = 0; i < codes.Length; ++i) {
            bindings[i] = (GamepadButton)codes[i];
        }
        return bindings;
    }

    // 指定操作のキーボード入力を置き換える
    public void SetKeyboardInputs(CanvasInputAction action, params KeyCode[] keys) {

        ArgumentNullException.ThrowIfNull(keys);
        int[] bindings = new int[keys.Length];
        for (int i = 0; i < keys.Length; ++i) {
            bindings[i] = (int)keys[i];
        }
        NativeApi.CanvasSetInputBindingsValue(
            entity.native, (int)action, KeyboardInputDevice, bindings);
    }

    // 指定操作のゲームパッド入力を置き換える
    public void SetGamepadInputs(CanvasInputAction action, params GamepadButton[] buttons) {

        ArgumentNullException.ThrowIfNull(buttons);
        int[] bindings = new int[buttons.Length];
        for (int i = 0; i < buttons.Length; ++i) {
            bindings[i] = (int)buttons[i];
        }
        NativeApi.CanvasSetInputBindingsValue(
            entity.native, (int)action, GamepadInputDevice, bindings);
    }

    // 指定操作へキーボード入力を追加する
    public void AddKeyboardInput(CanvasInputAction action, KeyCode key) {

        List<KeyCode> bindings = new(GetKeyboardInputs(action));
        if (!bindings.Contains(key)) {
            bindings.Add(key);
            SetKeyboardInputs(action, bindings.ToArray());
        }
    }

    // 指定操作へゲームパッド入力を追加する
    public void AddGamepadInput(CanvasInputAction action, GamepadButton button) {

        List<GamepadButton> bindings = new(GetGamepadInputs(action));
        if (!bindings.Contains(button)) {
            bindings.Add(button);
            SetGamepadInputs(action, bindings.ToArray());
        }
    }

    // 指定操作からキーボード入力を削除する
    public bool RemoveKeyboardInput(CanvasInputAction action, KeyCode key) {

        List<KeyCode> bindings = new(GetKeyboardInputs(action));
        if (!bindings.Remove(key)) {
            return false;
        }
        SetKeyboardInputs(action, bindings.ToArray());
        return true;
    }

    // 指定操作からゲームパッド入力を削除する
    public bool RemoveGamepadInput(CanvasInputAction action, GamepadButton button) {

        List<GamepadButton> bindings = new(GetGamepadInputs(action));
        if (!bindings.Remove(button)) {
            return false;
        }
        SetGamepadInputs(action, bindings.ToArray());
        return true;
    }

    // 指定操作のキーボード入力をすべて削除する
    public void ClearKeyboardInputs(CanvasInputAction action) {
        SetKeyboardInputs(action, Array.Empty<KeyCode>());
    }

    // 指定操作のゲームパッド入力をすべて削除する
    public void ClearGamepadInputs(CanvasInputAction action) {
        SetGamepadInputs(action, Array.Empty<GamepadButton>());
    }
}
