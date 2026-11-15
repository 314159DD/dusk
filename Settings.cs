using System.IO;
using System.Text.Json;

namespace Dusk;

internal sealed class Preset
{
    public int Kelvin { get; set; } = 6500;
    public int Brightness { get; set; } = 100;
}

internal sealed class MonitorSetting
{
    public const int PresetCount = 3;

    public int Kelvin { get; set; } = 6500;
    public int Brightness { get; set; } = 100;
    public int FilterKelvin { get; set; } = 3400;
    public int FilterReturnKelvin { get; set; } = 6500;
    public string Note { get; set; } = "";
    public List<Preset> Presets { get; set; } = new();

    public void Normalize()
    {
        Presets ??= new List<Preset>();
        Note ??= "";
        var defaults = new[]
        {
            new Preset { Kelvin = 6500, Brightness = 100 },
            new Preset { Kelvin = 4200, Brightness = 85 },
            new Preset { Kelvin = 2700, Brightness = 60 },
        };
        while (Presets.Count < PresetCount) Presets.Add(defaults[Presets.Count]);
    }
}

internal sealed class Settings
{
    public bool Enabled { get; set; } = true;
    public int KelvinStep { get; set; } = 200;
    public int BrightnessStep { get; set; } = 5;
    public Dictionary<string, MonitorSetting> Monitors { get; set; } = new();
    public Dictionary<string, string> Hotkeys { get; set; } = new();

    public static readonly Dictionary<string, string> DefaultHotkeys = new()
    {
        ["BrightnessUp"] = "Ctrl+Alt+Up",
        ["BrightnessDown"] = "Ctrl+Alt+Down",
        ["Cooler"] = "Ctrl+Alt+Right",
        ["Warmer"] = "Ctrl+Alt+Left",
        ["AllBrightnessUp"] = "Ctrl+Alt+Shift+Up",
        ["AllBrightnessDown"] = "Ctrl+Alt+Shift+Down",
        ["AllCooler"] = "Ctrl+Alt+Shift+Right",
        ["AllWarmer"] = "Ctrl+Alt+Shift+Left",
        ["Toggle"] = "Ctrl+Alt+Home",
    };

    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Dusk", "settings.json");

    public static Settings Load()
    {
        Settings settings;
        try
        {
            settings = File.Exists(FilePath)
                ? JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new Settings()
                : new Settings();
        }
        catch
        {
            settings = new Settings();
        }

        settings.Monitors ??= new Dictionary<string, MonitorSetting>();
        settings.Hotkeys ??= new Dictionary<string, string>();
        foreach (var monitor in settings.Monitors.Values) monitor.Normalize();
        foreach (var (action, keys) in DefaultHotkeys)
            settings.Hotkeys.TryAdd(action, keys);
        return settings;
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, JsonOptions));
        }
        catch
        {
        }
    }
}
