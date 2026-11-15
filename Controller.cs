using System.Windows.Threading;

namespace Dusk;

internal sealed class Controller
{
    public const int MinKelvin = 1200;
    public const int MaxKelvin = 9000;
    public const int MinBrightness = 10;
    public const int MaxBrightness = 100;

    private readonly DispatcherTimer _saveTimer;

    public Settings Settings { get; } = Settings.Load();
    public List<MonitorInfo> Monitors { get; private set; } = new();
    public event Action? Changed;

    public Controller()
    {
        _saveTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(600) };
        _saveTimer.Tick += (_, _) =>
        {
            _saveTimer.Stop();
            Settings.Save();
        };
    }

    public bool Enabled
    {
        get => Settings.Enabled;
        set
        {
            if (Settings.Enabled == value) return;
            Settings.Enabled = value;
            ApplyAll();
            QueueSave();
            Changed?.Invoke();
        }
    }

    public void Refresh()
    {
        Monitors = MonitorEnum.GetAll().OrderBy(m => m.Bounds.Left).ThenBy(m => m.Bounds.Top).ToList();
    }

    public MonitorSetting Get(MonitorInfo monitor)
    {
        if (!Settings.Monitors.TryGetValue(monitor.Id, out var setting))
        {
            Settings.Monitors[monitor.Id] = setting = new MonitorSetting();
            setting.Normalize();
        }
        return setting;
    }

    public bool IsPresetActive(MonitorInfo monitor, int index)
    {
        var setting = Get(monitor);
        var preset = setting.Presets[index];
        return preset.Kelvin == setting.Kelvin && preset.Brightness == setting.Brightness;
    }

    public void ApplyPreset(MonitorInfo monitor, int index)
    {
        var preset = Get(monitor).Presets[index];
        Set(monitor, preset.Kelvin, preset.Brightness);
    }

    public void SavePreset(MonitorInfo monitor, int index)
    {
        var setting = Get(monitor);
        setting.Presets[index] = new Preset { Kelvin = setting.Kelvin, Brightness = setting.Brightness };
        QueueSave();
        Changed?.Invoke();
    }

    public bool IsFilterActive(MonitorInfo monitor)
    {
        var setting = Get(monitor);
        return setting.Kelvin == setting.FilterKelvin;
    }

    public void ToggleFilter(MonitorInfo monitor)
    {
        var setting = Get(monitor);
        if (IsFilterActive(monitor))
        {
            Set(monitor, setting.FilterReturnKelvin, null);
            return;
        }
        setting.FilterReturnKelvin = setting.Kelvin;
        Set(monitor, setting.FilterKelvin, null);
    }

    public void SaveFilter(MonitorInfo monitor)
    {
        var setting = Get(monitor);
        setting.FilterKelvin = setting.Kelvin;
        QueueSave();
        Changed?.Invoke();
    }

    public void Set(MonitorInfo monitor, int? kelvin, int? brightness)
    {
        var setting = Get(monitor);
        if (kelvin is { } k) setting.Kelvin = Math.Clamp(k, MinKelvin, MaxKelvin);
        if (brightness is { } b) setting.Brightness = Math.Clamp(b, MinBrightness, MaxBrightness);
        Apply(monitor);
        QueueSave();
        Changed?.Invoke();
    }

    public void ResetAll()
    {
        foreach (var monitor in Monitors)
        {
            var setting = Get(monitor);
            setting.Kelvin = 6500;
            setting.Brightness = 100;
        }
        ApplyAll();
        QueueSave();
        Changed?.Invoke();
    }

    public void Apply(MonitorInfo monitor)
    {
        if (!Settings.Enabled)
        {
            Gamma.Apply(monitor.DeviceName, 1, 1, 1);
            return;
        }
        var setting = Get(monitor);
        var (r, g, b) = ColorMath.FromKelvin(setting.Kelvin);
        var level = setting.Brightness / 100.0;
        Gamma.Apply(monitor.DeviceName, r * level, g * level, b * level);
    }

    public void ApplyAll()
    {
        foreach (var monitor in Monitors) Apply(monitor);
    }

    public void RestoreAll()
    {
        foreach (var monitor in Monitors) Gamma.Apply(monitor.DeviceName, 1, 1, 1);
    }

    public MonitorInfo? MonitorUnderCursor()
    {
        if (Monitors.Count == 0) return null;
        if (Native.GetCursorPos(out var point))
        {
            var handle = Native.MonitorFromPoint(point, Native.MONITOR_DEFAULTTONEAREST);
            var match = Monitors.FirstOrDefault(m => m.Handle == handle);
            if (match != null) return match;
        }
        return Monitors.FirstOrDefault(m => m.Primary) ?? Monitors[0];
    }

    public void Flush()
    {
        _saveTimer.Stop();
        Settings.Save();
    }

    private void QueueSave()
    {
        _saveTimer.Stop();
        _saveTimer.Start();
    }
}
