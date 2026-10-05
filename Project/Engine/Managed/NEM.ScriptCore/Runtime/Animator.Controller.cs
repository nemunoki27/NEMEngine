namespace NEMEngine;

// ControllerのParameterを名前と型で操作する
public sealed partial class Animator {

    public void SetFloat(string name, float value) {

        if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
        NativeAnimatorAPI.SetParameter(native, name, 0, value, 0);
    }

    public float GetFloat(string name) {

        NativeAnimatorAPI.GetParameter(native, name, 0, out float value, out _);
        return value;
    }

    public void SetInteger(string name, int value) {

        NativeAnimatorAPI.SetParameter(native, name, 1, 0.0f, value);
    }

    public int GetInteger(string name) {

        NativeAnimatorAPI.GetParameter(native, name, 1, out _, out int value);
        return value;
    }

    public void SetBool(string name, bool value) {

        NativeAnimatorAPI.SetParameter(native, name, 2, 0.0f, value ? 1 : 0);
    }

    public bool GetBool(string name) {

        NativeAnimatorAPI.GetParameter(native, name, 2, out _, out int value);
        return value != 0;
    }

    public void SetTrigger(string name) {

        NativeAnimatorAPI.SetParameter(native, name, 3, 0.0f, 1);
    }

    public void ResetTrigger(string name) {

        NativeAnimatorAPI.SetParameter(native, name, 3, 0.0f, 0);
    }
}
