using System.ComponentModel;
using System.Windows;
using System.Windows.Media.Animation;

namespace Dusk;

internal sealed class TileVm : INotifyPropertyChanged
{
    private readonly Func<bool> _active;
    private readonly Func<bool> _armed;
    private readonly Func<string> _tip;
    private readonly Action _press;

    public TileVm(string label, Func<bool> active, Func<bool> armed, Func<string> tip, Action press)
    {
        Label = label;
        _active = active;
        _armed = armed;
        _tip = tip;
        _press = press;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public string Label { get; }
    public bool IsActive => _active();
    public bool IsArmed => _armed();
    public string Tip => _tip();

    public void Press() => _press();
    public void Raise() => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(string.Empty));
}

internal sealed class MonitorVm : INotifyPropertyChanged
{
    private readonly Controller _controller;
    private readonly MonitorInfo _monitor;
    private bool _saveMode;

    public MonitorVm(Controller controller, MonitorInfo monitor)
    {
        _controller = controller;
        _monitor = monitor;

        Slots = Enumerable.Range(0, MonitorSetting.PresetCount).Select(index => new TileVm(
            (index + 1).ToString(),
            () => _controller.IsPresetActive(_monitor, index),
            () => _saveMode,
            () =>
            {
                var preset = _controller.Get(_monitor).Presets[index];
                return _saveMode
                    ? $"Store the current values in {index + 1}"
                    : $"{preset.Kelvin} K, {preset.Brightness} %";
            },
            () => Press(() => _controller.SavePreset(_monitor, index), () => _controller.ApplyPreset(_monitor, index)))).ToList();

        Filter = new TileVm(
            "Blue light filter",
            () => _controller.IsFilterActive(_monitor),
            () => _saveMode,
            () => _saveMode
                ? "Use the current temperature as this screen's filter"
                : $"Sets this screen to {_controller.Get(_monitor).FilterKelvin} K. Click again to go back.",
            () => Press(() => _controller.SaveFilter(_monitor), () => _controller.ToggleFilter(_monitor)));
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public List<TileVm> Slots { get; }
    public TileVm Filter { get; }

    public bool SaveMode
    {
        get => _saveMode;
        set
        {
            if (_saveMode == value) return;
            _saveMode = value;
            Raise();
        }
    }

    public string Note
    {
        get => _controller.Get(_monitor).Note;
        set => _controller.SetNote(_monitor, value);
    }

    public bool HasNote => Note.Length > 0;
    public string NoteText => HasNote ? Note : "Add note";

    private void Press(Action store, Action apply)
    {
        if (_saveMode)
        {
            _saveMode = false;
            store();
            return;
        }
        apply();
    }

    public string Name => _monitor.Name;
    public string Detail => $"{_monitor.Bounds.Width} × {_monitor.Bounds.Height}";

    public double Kelvin
    {
        get => _controller.Get(_monitor).Kelvin;
        set => _controller.Set(_monitor, (int)Math.Round(value / 50) * 50, null);
    }

    public double Brightness
    {
        get => _controller.Get(_monitor).Brightness;
        set => _controller.Set(_monitor, null, (int)Math.Round(value));
    }

    public string KelvinText => $"{_controller.Get(_monitor).Kelvin} K";
    public string BrightnessText => $"{_controller.Get(_monitor).Brightness} %";

    public void Raise()
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(string.Empty));
        foreach (var slot in Slots) slot.Raise();
        Filter.Raise();
    }
}

public partial class PanelWindow : Window
{
    private readonly Controller _controller;
    private readonly Action _quit;
    private List<MonitorVm> _items = new();

    public DateTime LastHidden { get; private set; } = DateTime.MinValue;

    internal PanelWindow(Controller controller, IReadOnlyList<string> failedHotkeys, Action quit)
    {
        InitializeComponent();
        _controller = controller;
        _quit = quit;

        var keys = controller.Settings.Hotkeys;
        HintText.Text =
            $"Brightness: {keys["BrightnessUp"]}, {keys["BrightnessDown"]}\n" +
            $"Temperature: {keys["Warmer"]}, {keys["Cooler"]}\n" +
            $"On or off: {keys["Toggle"]}\n" +
            "Keys act on the screen under the mouse. Add Shift for all screens.";

        if (failedHotkeys.Count > 0)
        {
            WarnText.Text = "Could not register: " + string.Join(", ", failedHotkeys) + ". Another program holds these keys.";
            WarnText.Visibility = Visibility.Visible;
        }

        Rebuild();
        _controller.Changed += Sync;
        Deactivated += (_, _) => HidePanel();
        SizeChanged += (_, _) => Place();
    }

    public void Rebuild()
    {
        _items = _controller.Monitors.Select(m => new MonitorVm(_controller, m)).ToList();
        MonitorList.ItemsSource = _items;
        Sync();
    }

    private void Sync()
    {
        foreach (var item in _items) item.Raise();
        EnabledSwitch.IsChecked = _controller.Enabled;
        StateText.Text = _controller.Enabled ? "On" : "Paused";
        AutostartSwitch.IsChecked = Autostart.IsEnabled;
    }

    public void ShowPanel()
    {
        Sync();
        Show();
        Place();
        Activate();

        if (!SystemParameters.ClientAreaAnimation)
        {
            Root.Opacity = 1;
            return;
        }

        var spring = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.45 };
        var time = TimeSpan.FromMilliseconds(250);
        RootScale.BeginAnimation(System.Windows.Media.ScaleTransform.ScaleXProperty, new DoubleAnimation(0.96, 1, time) { EasingFunction = spring });
        RootScale.BeginAnimation(System.Windows.Media.ScaleTransform.ScaleYProperty, new DoubleAnimation(0.96, 1, time) { EasingFunction = spring });
        RootShift.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty, new DoubleAnimation(10, 0, time) { EasingFunction = spring });
        Root.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(150)));
    }

    public void HidePanel()
    {
        if (!IsVisible) return;
        LastHidden = DateTime.UtcNow;
        foreach (var item in _items) item.SaveMode = false;
        Hide();
    }

    private void OnTileClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: TileVm tile }) tile.Press();
    }

    private void OnNoteMouseDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (e.ClickCount != 2 || sender is not FrameworkElement { DataContext: MonitorVm monitor, Parent: System.Windows.Controls.Panel host }) return;
        var box = host.Children.OfType<System.Windows.Controls.TextBox>().First();
        box.Text = monitor.Note;
        box.Visibility = Visibility.Visible;
        ((UIElement)sender).Visibility = Visibility.Collapsed;
        box.Focus();
        box.SelectAll();
        e.Handled = true;
    }

    private void OnNoteKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (sender is not System.Windows.Controls.TextBox box) return;
        if (e.Key == System.Windows.Input.Key.Enter)
        {
            EndNoteEdit(box, commit: true);
            e.Handled = true;
        }
        else if (e.Key == System.Windows.Input.Key.Escape)
        {
            EndNoteEdit(box, commit: false);
            e.Handled = true;
        }
    }

    private void OnNoteLostFocus(object sender, RoutedEventArgs e)
    {
        if (sender is System.Windows.Controls.TextBox box) EndNoteEdit(box, commit: true);
    }

    private static void EndNoteEdit(System.Windows.Controls.TextBox box, bool commit)
    {
        if (box.Visibility != Visibility.Visible) return;
        box.Visibility = Visibility.Collapsed;
        if (box.Parent is System.Windows.Controls.Panel host)
            foreach (var label in host.Children.OfType<System.Windows.Controls.TextBlock>()) label.Visibility = Visibility.Visible;
        if (commit && box.DataContext is MonitorVm monitor) monitor.Note = box.Text;
    }

    private void OnSaveClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: MonitorVm monitor }) monitor.SaveMode = !monitor.SaveMode;
    }

    internal bool AutoPlace { get; set; } = true;

    private void Place()
    {
        if (!AutoPlace) return;
        var area = SystemParameters.WorkArea;
        Left = area.Right - ActualWidth;
        Top = area.Bottom - ActualHeight;
    }

    private void OnEnabledClick(object sender, RoutedEventArgs e) => _controller.Enabled = EnabledSwitch.IsChecked == true;

    private void OnAutostartClick(object sender, RoutedEventArgs e) => Autostart.Set(AutostartSwitch.IsChecked == true);

    private void OnResetClick(object sender, RoutedEventArgs e) => _controller.ResetAll();

    private void OnQuitClick(object sender, RoutedEventArgs e) => _quit();
}
