using System.Drawing.Drawing2D;
using Microsoft.Win32;

namespace MultiRemoteTray;

// The tray icon, its menu, and the loop that keeps the remote up to date.
sealed class TrayContext : ApplicationContext
{
    const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    const string RunName = "MultiRemoteTray";

    readonly NotifyIcon _icon;
    readonly ToolStripMenuItem _status;
    readonly ToolStripMenuItem _autostart;
    readonly SynchronizationContext _ui;

    readonly RemoteLink _link = new();
    readonly MediaWatcher _media = new();
    readonly SemaphoreSlim _wake = new(0);
    volatile bool _dirty = true;
    readonly CancellationTokenSource _stop = new();

    public TrayContext()
    {
        _status = new ToolStripMenuItem("Starting...") { Enabled = false };
        _autostart = new ToolStripMenuItem("Start with Windows", null, (_, _) => ToggleAutostart())
        {
            Checked = IsAutostart()
        };
        var menu = new ContextMenuStrip();
        menu.Items.Add(_status);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Send track info now", null, (_, _) => { _link.ResetCache(); Poke(); });
        menu.Items.Add(_autostart);
        menu.Items.Add("Open log", null, (_, _) => OpenLog());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => Quit());

        // Creating the menu installs the WinForms synchronization context.
        _ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();

        _icon = new NotifyIcon
        {
            Icon = MakeIcon(),
            Text = "MultiRemote",
            ContextMenuStrip = menu,
            Visible = true,
        };

        _media.Changed += Poke;
        _link.StatusChanged += () => _ui.Post(_ => ShowStatus(), null);
        ShowStatus();
        _ = Task.Run(() => LoopAsync(_stop.Token));
    }

    void Poke()
    {
        _dirty = true;
        _wake.Release();
    }

    async Task LoopAsync(CancellationToken ct)
    {
        Log.Write($"--- MultiRemote tray app started (Windows {Environment.OSVersion.Version})");
        try { await _media.StartAsync(); }
        catch (Exception ex) { Log.Error("media start", ex); _link.SetStatus("Cannot read Windows media: " + ex.Message); }

        while (!ct.IsCancellationRequested)
        {
            try
            {
                if (!_link.Connected && await _link.TryConnectAsync())
                    _dirty = true;   // a (re)connected remote knows nothing yet

                if (_link.Connected && _dirty)
                {
                    _dirty = false;
                    await Task.Delay(300, ct);   // let a burst of media events settle
                    var np = await _media.ReadAsync();
                    if (!await _link.SendAsync(np)) _dirty = true;
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex) { Log.Error("loop", ex); _link.SetStatus("Error: " + ex.Message); }

            // Re-check every few seconds, or at once when the music changes.
            try { await _wake.WaitAsync(_link.Connected ? 5000 : 10000, ct); }
            catch (OperationCanceledException) { break; }
        }
    }

    static void OpenLog()
    {
        Log.Write("(log opened)");
        try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(Log.File) { UseShellExecute = true }); }
        catch (Exception ex) { MessageBox.Show("Could not open " + Log.File + "\n" + ex.Message, "MultiRemote"); }
    }

    void ShowStatus()
    {
        _status.Text = _link.Status;
        var tip = "MultiRemote - " + _link.Status;
        _icon.Text = tip.Length > 63 ? tip[..63] : tip;
    }

    static bool IsAutostart()
    {
        using var k = Registry.CurrentUser.OpenSubKey(RunKey);
        return k?.GetValue(RunName) is string;
    }

    void ToggleAutostart()
    {
        using var k = Registry.CurrentUser.CreateSubKey(RunKey);
        if (IsAutostart()) k.DeleteValue(RunName, false);
        else k.SetValue(RunName, "\"" + Environment.ProcessPath + "\"");
        _autostart.Checked = IsAutostart();
    }

    void Quit()
    {
        _stop.Cancel();
        _icon.Visible = false;
        _icon.Dispose();
        _link.Dispose();
        ExitThread();
    }

    // A small remote-shaped icon, drawn here so there is no .ico to ship.
    static Icon MakeIcon()
    {
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);
            using var body = new SolidBrush(Color.FromArgb(0x1B, 0x20, 0x29));
            using var path = RoundRect(new Rectangle(7, 1, 18, 30), 5);
            g.FillPath(body, path);
            using var amber = new SolidBrush(Color.FromArgb(0xF5, 0x9E, 0x0B));
            using var blue = new SolidBrush(Color.FromArgb(0x4C, 0x9A, 0xFF));
            g.FillRectangle(blue, 10, 5, 12, 8);
            g.FillEllipse(amber, 10, 16, 5, 5);
            g.FillEllipse(amber, 17, 16, 5, 5);
            g.FillEllipse(amber, 10, 23, 5, 5);
            g.FillEllipse(amber, 17, 23, 5, 5);
        }
        return Icon.FromHandle(bmp.GetHicon());
    }

    static GraphicsPath RoundRect(Rectangle r, int radius)
    {
        var p = new GraphicsPath();
        int d = radius * 2;
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }
}
