// Scripta Sync Server: a tray app that runs `rclone serve webdav` for the Scripta writerdeck.
// Build: see build.ps1 (uses the .NET Framework csc.exe that ships with Windows).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Management;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

static class Program {
  [STAThread]
  static void Main(string[] args) {
    bool created;
    using (var mutex = new Mutex(true, "ScriptaSyncServer.SingleInstance", out created)) {
      var showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, "ScriptaSyncServer.Show");
      if (!created) { showEvent.Set(); return; }  // already running: bring its window up
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);
      bool startHidden = args.Contains("--tray");
      Application.Run(new TrayApp(showEvent, startHidden));
    }
  }
}

// ------------------------------------------------------------------ palette
static class Pal {
  public static readonly Color Sage = Color.FromArgb(163, 177, 138);
  public static readonly Color SageDeep = Color.FromArgb(107, 122, 90);
  public static readonly Color SageMist = Color.FromArgb(226, 231, 217);
  public static readonly Color Paper = Color.FromArgb(245, 246, 241);
  public static readonly Color Charcoal = Color.FromArgb(47, 51, 55);
  public static readonly Color CharcoalSoft = Color.FromArgb(74, 79, 84);
  public static readonly Color Brown = Color.FromArgb(123, 99, 82);
  public static readonly Color BrownSoft = Color.FromArgb(150, 132, 118);
  public static readonly Color Orange = Color.FromArgb(192, 97, 43);
  public static readonly Color Icy = Color.FromArgb(236, 244, 247);
  public static readonly Color IcyMid = Color.FromArgb(168, 200, 214);
  // status: muted takes on the usual green / red / amber
  public static readonly Color Ok = Color.FromArgb(108, 160, 104);
  public static readonly Color Off = Color.FromArgb(184, 98, 90);
  public static readonly Color Busy = Color.FromArgb(204, 166, 78);
}

// ------------------------------------------------------------------ settings
class Settings {
  public readonly string Path;
  public string RclonePath = "";
  public string Folder = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Scripta");
  public int Port = 8080;
  public string User = "scripta";
  public string Password = "";
  public bool StartServerOnLaunch = false;
  public bool LaunchAtLogin = false;

  public Settings(string path) {
    Path = path;
    if (File.Exists(path)) { Load(); return; }
    // first run: a default folder and a random, easy-to-type password
    Password = MakePassword();
    try { Directory.CreateDirectory(Folder); } catch { }
    try { Save(); } catch { }
  }

  public static string MakePassword() {
    string[] words = { "apple", "river", "candle", "forest", "pebble", "garden", "lantern", "meadow", "harbor", "violet",
      "cobalt", "maple", "otter", "sparrow", "willow", "copper", "ember", "thistle", "canyon", "juniper", "orchid",
      "falcon", "marble", "saffron", "cedar", "glacier", "basil", "comet", "harvest", "island", "velvet", "walnut",
      "breeze", "clover", "dune", "fern", "hazel", "indigo", "jasper", "kettle", "lemon", "mango", "nutmeg", "olive",
      "pepper", "quartz", "raven", "sage", "tulip", "umber", "wren", "yarrow", "zephyr", "acorn", "badger", "cinder",
      "delta", "echo", "fable", "grove" };
    var b = new byte[4];
    using (var rng = new System.Security.Cryptography.RNGCryptoServiceProvider()) rng.GetBytes(b);
    return string.Join("-", b.Select(x => words[x % words.Length]));
  }

  public void Load() {
    if (!File.Exists(Path)) return;
    foreach (var raw in File.ReadAllLines(Path)) {
      var line = raw.Trim();
      if (line.Length == 0 || line.StartsWith("#") || line.StartsWith(";")) continue;
      int eq = line.IndexOf('=');
      if (eq < 0) continue;
      string k = line.Substring(0, eq).Trim().ToLowerInvariant(), v = line.Substring(eq + 1).Trim();
      switch (k) {
        case "rclonepath": RclonePath = v; break;
        case "folder": Folder = v; break;
        case "port": int.TryParse(v, out Port); break;
        case "user": User = v; break;
        case "password": Password = v; break;
        case "startserveronlaunch": StartServerOnLaunch = IsTrue(v); break;
        case "launchatlogin": LaunchAtLogin = IsTrue(v); break;
      }
    }
  }

  static bool IsTrue(string v) { v = v.ToLowerInvariant(); return v == "true" || v == "1" || v == "yes"; }

  public void Save() {
    File.WriteAllLines(Path, new[] {
      "# Scripta Sync Server settings (edit while the app is closed, or use its window)",
      "RclonePath=" + RclonePath,
      "Folder=" + Folder,
      "Port=" + Port,
      "User=" + User,
      "Password=" + Password,
      "StartServerOnLaunch=" + (StartServerOnLaunch ? "true" : "false"),
      "LaunchAtLogin=" + (LaunchAtLogin ? "true" : "false"),
    });
  }
}

// ------------------------------------------------------------------ server control
class Server {
  readonly Settings S;
  readonly string LogPath;
  Process proc;
  public string LastError = "";

  public Server(Settings s, string logPath) { S = s; LogPath = logPath; }

  public bool IsListening() {
    try {
      using (var c = new TcpClient()) {
        var ar = c.BeginConnect(IPAddress.Loopback, S.Port, null, null);
        bool ok = ar.AsyncWaitHandle.WaitOne(300) && c.Connected;
        if (ok) c.EndConnect(ar);
        return ok;
      }
    } catch { return false; }
  }

  string ResolveRclone() {
    if (!string.IsNullOrEmpty(S.RclonePath) && File.Exists(S.RclonePath)) return S.RclonePath;
    string pkgs = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Microsoft\WinGet\Packages");
    try {
      if (Directory.Exists(pkgs)) {
        var hit = Directory.GetDirectories(pkgs, "Rclone.Rclone*")
          .SelectMany(d => Directory.GetFiles(d, "rclone.exe", SearchOption.AllDirectories))
          .OrderByDescending(f => f).FirstOrDefault();
        if (hit != null) return hit;
      }
    } catch { }
    return "rclone.exe";  // fall back to PATH
  }

  static string Q(string s) { return "\"" + s.Replace("\"", "\\\"") + "\""; }

  public bool Start() {
    LastError = "";
    if (IsListening()) return true;
    if (!Directory.Exists(S.Folder)) { LastError = "Folder not found:\n" + S.Folder; return false; }
    if (string.IsNullOrEmpty(S.Password)) { LastError = "Set a password first."; return false; }
    var psi = new ProcessStartInfo {
      FileName = ResolveRclone(),
      Arguments = string.Join(" ", new[] {
        "serve", "webdav", Q(S.Folder), "--addr", ":" + S.Port,
        "--user", Q(S.User), "--pass", Q(S.Password),
        "--dir-cache-time", "1s", "--log-file", Q(LogPath), "--log-level", "INFO" }),
      UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
    };
    try { proc = Process.Start(psi); }
    catch (Exception e) {
      LastError = "Could not start rclone (" + e.Message + ").\n\nInstall it with:  winget install Rclone.Rclone\nor set RclonePath in settings.ini.";
      return false;
    }
    for (int i = 0; i < 40; i++) {
      if (IsListening()) return true;
      if (proc.HasExited) { LastError = "rclone stopped right away (exit " + proc.ExitCode + "). See the log."; return false; }
      Thread.Sleep(250);
    }
    LastError = "rclone did not start listening on port " + S.Port + ". See the log.";
    return false;
  }

  public void Stop() {
    try { if (proc != null && !proc.HasExited) { proc.Kill(); proc.WaitForExit(3000); } } catch { }
    proc = null;
    // also stop a server started some other way (e.g. an older launcher)
    try {
      using (var q = new ManagementObjectSearcher("SELECT ProcessId, CommandLine FROM Win32_Process WHERE Name = 'rclone.exe'"))
        foreach (ManagementObject o in q.Get()) {
          string cl = (o["CommandLine"] as string) ?? "";
          if (cl.Contains("serve webdav") && cl.Contains(":" + S.Port)) {
            try { Process.GetProcessById(Convert.ToInt32(o["ProcessId"])).Kill(); } catch { }
          }
        }
    } catch { }
    for (int i = 0; i < 20 && IsListening(); i++) Thread.Sleep(150);
  }
}

// ------------------------------------------------------------------ tray + window
class TrayApp : ApplicationContext {
  const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
  const string RunName = "ScriptaSyncServer";

  readonly string dir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
  readonly Settings S;
  readonly Server server;
  readonly NotifyIcon tray;
  readonly Icon baseIcon, iconOn, iconOff;
  readonly MainForm form;
  readonly ToolStripMenuItem miToggle;
  readonly System.Windows.Forms.Timer poll;
  bool running;

  public TrayApp(EventWaitHandle showEvent, bool startHidden) {
    S = new Settings(Path.Combine(dir, "settings.ini"));
    server = new Server(S, Path.Combine(dir, "rclone.log"));

    baseIcon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
    iconOn = Badge(Pal.Ok);
    iconOff = Badge(Pal.Off);

    form = new MainForm(this, S, baseIcon);
    var forceHandle = form.Handle;  // BeginInvoke from the show-event thread needs a handle even while hidden

    var menu = new ContextMenuStrip();
    menu.Items.Add("Open Scripta Sync Server", null, (s, e) => ShowWindow()).Font = new Font(menu.Font, FontStyle.Bold);
    menu.Items.Add(new ToolStripSeparator());
    miToggle = new ToolStripMenuItem("Start server", null, (s, e) => Toggle());
    menu.Items.Add(miToggle);
    menu.Items.Add("Restart server", null, (s, e) => Restart());
    menu.Items.Add("Copy device URL", null, (s, e) => CopyUrl());
    menu.Items.Add("Open in browser", null, (s, e) => OpenBrowser());
    menu.Items.Add(new ToolStripSeparator());
    menu.Items.Add("Quit (stops the server)", null, (s, e) => Quit());

    tray = new NotifyIcon { Icon = iconOff, Visible = true, ContextMenuStrip = menu, Text = "Scripta Sync Server" };
    tray.MouseClick += (s, e) => { if (e.Button == MouseButtons.Left) ShowWindow(); };

    // a second launch signals us to show the window
    ThreadPool.RegisterWaitForSingleObject(showEvent, (st, to) => form.BeginInvoke((Action)ShowWindow), null, -1, false);

    poll = new System.Windows.Forms.Timer { Interval = 3000 };
    poll.Tick += (s, e) => Refresh();
    poll.Start();

    EnsureShortcuts();
    PromoteTrayIcon();
    SyncRunKey();
    Refresh();

    if (S.StartServerOnLaunch && !running) StartServer(quiet: true);
    if (!startHidden) ShowWindow();
  }

  Icon Badge(Color c) {
    using (var bmp = new Bitmap(32, 32)) {
      using (var g = Graphics.FromImage(bmp)) {
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.DrawIcon(new Icon(baseIcon, 32, 32), new Rectangle(0, 0, 32, 32));
        g.FillEllipse(new SolidBrush(Pal.Charcoal), 17, 17, 15, 15);
        g.FillEllipse(new SolidBrush(c), 19, 19, 11, 11);
      }
      return Icon.FromHandle(bmp.GetHicon());
    }
  }

  public string LanIp() {
    try {
      using (var s = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, 0)) {
        s.Connect("10.255.255.255", 1);  // no packet is sent; picks the outward-facing address
        return ((IPEndPoint)s.LocalEndPoint).Address.ToString();
      }
    } catch { return "localhost"; }
  }
  public string Url() { return "http://" + LanIp() + ":" + S.Port; }

  public void Refresh() {
    running = server.IsListening();
    tray.Icon = running ? iconOn : iconOff;
    tray.Text = running ? "Scripta Sync Server: running" : "Scripta Sync Server: stopped";
    miToggle.Text = running ? "Stop server" : "Start server";
    form.ShowState(running, Url());
  }

  public void StartServer(bool quiet = false) {
    form.Busy("Starting...");
    bool ok = server.Start();
    Refresh();
    if (!ok) {
      if (quiet) tray.ShowBalloonTip(5000, "Scripta Sync Server", server.LastError, ToolTipIcon.Error);
      else MessageBox.Show(server.LastError, "Scripta Sync Server", MessageBoxButtons.OK, MessageBoxIcon.Error);
    }
  }
  public void StopServer() { form.Busy("Stopping..."); server.Stop(); Refresh(); }
  public void Toggle() { if (server.IsListening()) StopServer(); else StartServer(); }
  public void Restart() { StopServer(); StartServer(); }
  public void CopyUrl() { Clipboard.SetText(Url()); tray.ShowBalloonTip(2000, "Copied", Url() + "/<folder>", ToolTipIcon.Info); }
  public void OpenBrowser() { Process.Start("http://localhost:" + S.Port + "/"); }
  public void OpenLog() {
    string log = Path.Combine(dir, "rclone.log");
    if (File.Exists(log)) Process.Start("notepad.exe", "\"" + log + "\""); else MessageBox.Show("No log yet.", "Scripta Sync Server");
  }
  public void OpenFolder() { Process.Start("explorer.exe", "\"" + S.Folder + "\""); }

  public void SettingsChanged(bool restartIfRunning) {
    S.Save();
    SyncRunKey();
    if (restartIfRunning && server.IsListening()) Restart();
  }

  void SyncRunKey() {
    try {
      using (var k = Registry.CurrentUser.CreateSubKey(RunKey)) {
        if (S.LaunchAtLogin) k.SetValue(RunName, "\"" + Application.ExecutablePath + "\" --tray");
        else if (k.GetValue(RunName) != null) k.DeleteValue(RunName);
      }
    } catch { }
  }

  void EnsureShortcuts() {
    string exe = Application.ExecutablePath;
    var targets = new[] {
      Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Scripta Sync Server.lnk"),
      Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Scripta Sync Server.lnk"),
    };
    try {
      Type t = Type.GetTypeFromProgID("WScript.Shell");
      dynamic sh = Activator.CreateInstance(t);
      foreach (var lnk in targets) {
        dynamic sc = sh.CreateShortcut(lnk);
        if (File.Exists(lnk) && string.Equals((string)sc.TargetPath, exe, StringComparison.OrdinalIgnoreCase)) continue;
        sc.TargetPath = exe;
        sc.Arguments = "";
        sc.WorkingDirectory = dir;
        sc.IconLocation = exe + ",0";
        sc.Description = "Start, stop or check the Scripta WebDAV sync server";
        sc.Save();
      }
    } catch { }
  }

  // Windows 11 tucks new tray icons into the overflow menu; mark ours as shown on the taskbar.
  void PromoteTrayIcon() {
    try {
      using (var root = Registry.CurrentUser.OpenSubKey(@"Control Panel\NotifyIconSettings", true)) {
        if (root == null) return;
        foreach (var name in root.GetSubKeyNames())
          using (var k = root.OpenSubKey(name, true)) {
            var p = k == null ? null : k.GetValue("ExecutablePath") as string;
            if (p != null && string.Equals(p, Application.ExecutablePath, StringComparison.OrdinalIgnoreCase)
                && !(k.GetValue("IsPromoted") is int && (int)k.GetValue("IsPromoted") == 1))
              k.SetValue("IsPromoted", 1, RegistryValueKind.DWord);
          }
      }
    } catch { }
  }

  public void ShowWindow() {
    form.Show();
    if (form.WindowState == FormWindowState.Minimized) form.WindowState = FormWindowState.Normal;
    form.Activate();
    Refresh();
    PromoteTrayIcon();  // the entry only exists after the icon has been shown once
  }

  public void Quit() {
    poll.Stop();
    if (server.IsListening()) server.Stop();
    tray.Visible = false;
    form.ReallyClose();
    ExitThread();
  }
}

class MainForm : Form {
  readonly TrayApp app;
  readonly Settings S;
  readonly Label lblState, lblDot;
  readonly TextBox txtUrl, txtUser, txtPass, txtFolder;
  readonly Button btnToggle, btnRestart, btnShowPass;
  readonly CheckBox chkStart, chkLogin;
  bool reallyClose, loading;

  public MainForm(TrayApp app, Settings s, Icon icon) {
    this.app = app; S = s;
    Text = "Scripta Sync Server";
    Icon = icon;
    FormBorderStyle = FormBorderStyle.FixedSingle;
    MaximizeBox = false;
    StartPosition = FormStartPosition.CenterScreen;
    Font = new Font("Segoe UI", 9.5f);
    AutoScaleMode = AutoScaleMode.Dpi;
    ClientSize = new Size(440, 400);
    BackColor = Pal.Paper;
    ForeColor = Pal.Charcoal;

    // header
    var header = new Panel { Dock = DockStyle.Top, Height = 72, BackColor = Pal.Charcoal };
    var pic = new PictureBox { Image = new Icon(icon, 48, 48).ToBitmap(), SizeMode = PictureBoxSizeMode.Zoom, Bounds = new Rectangle(14, 12, 48, 48), BackColor = Color.Transparent };
    var title = new Label { Text = "Scripta Sync Server", ForeColor = Pal.Icy, Font = new Font("Segoe UI Semibold", 13f), AutoSize = true, Location = new Point(72, 12), BackColor = Color.Transparent };
    lblDot = new Label { Text = "●", ForeColor = Pal.Off, Font = new Font("Segoe UI", 11f), AutoSize = true, Location = new Point(73, 39), BackColor = Color.Transparent };
    lblState = new Label { Text = "Checking...", ForeColor = Pal.IcyMid, AutoSize = true, Location = new Point(93, 42), BackColor = Color.Transparent };
    header.Controls.AddRange(new Control[] { pic, title, lblDot, lblState });
    Controls.Add(header);

    int y = 88, lx = 16, fx = 120, fw = 304;
    Func<string, int, Label> label = (t, yy) => { var l = new Label { Text = t, AutoSize = true, Location = new Point(lx, yy + 4), ForeColor = Pal.Brown }; Controls.Add(l); return l; };

    label("Device URL", y);
    txtUrl = new TextBox { ReadOnly = true, Bounds = new Rectangle(fx, y, fw - 64, 24), BackColor = Pal.Icy };
    var btnCopy = new Button { Text = "Copy", Bounds = new Rectangle(fx + fw - 58, y - 1, 58, 27) };
    btnCopy.Click += (s2, e) => app.CopyUrl();
    Controls.AddRange(new Control[] { txtUrl, btnCopy });
    y += 30;
    var hint = new Label { Text = "On the device add a vault folder, e.g. …:" + S.Port + "/Scripta", AutoSize = true, Location = new Point(fx, y), ForeColor = Pal.BrownSoft, Font = new Font("Segoe UI", 8.5f) };
    Controls.Add(hint);
    y += 28;

    label("User", y);
    txtUser = new TextBox { Bounds = new Rectangle(fx, y, fw, 24), Text = S.User };
    Controls.Add(txtUser);
    y += 32;

    label("Password", y);
    txtPass = new TextBox { Bounds = new Rectangle(fx, y, fw - 64, 24), Text = S.Password, UseSystemPasswordChar = true };
    btnShowPass = new Button { Text = "Show", Bounds = new Rectangle(fx + fw - 58, y - 1, 58, 27) };
    btnShowPass.Click += (s2, e) => { txtPass.UseSystemPasswordChar = !txtPass.UseSystemPasswordChar; btnShowPass.Text = txtPass.UseSystemPasswordChar ? "Show" : "Hide"; };
    Controls.AddRange(new Control[] { txtPass, btnShowPass });
    y += 32;

    label("Vault folder", y);
    txtFolder = new TextBox { Bounds = new Rectangle(fx, y, fw - 64, 24), Text = S.Folder };
    var btnBrowse = new Button { Text = "...", Bounds = new Rectangle(fx + fw - 58, y - 1, 58, 27) };
    btnBrowse.Click += (s2, e) => {
      using (var d = new FolderBrowserDialog { SelectedPath = txtFolder.Text, Description = "Folder to share with the Scripta device" })
        if (d.ShowDialog(this) == DialogResult.OK) { txtFolder.Text = d.SelectedPath; CommitFields(); }
    };
    Controls.AddRange(new Control[] { txtFolder, btnBrowse });
    y += 40;

    foreach (var tb in new[] { txtUser, txtPass, txtFolder }) tb.Leave += (s2, e) => CommitFields();

    chkStart = new CheckBox { Text = "Start the server when this app opens", AutoSize = true, Location = new Point(lx, y), Checked = S.StartServerOnLaunch };
    chkStart.CheckedChanged += (s2, e) => { if (loading) return; S.StartServerOnLaunch = chkStart.Checked; app.SettingsChanged(false); };
    Controls.Add(chkStart);
    y += 26;
    chkLogin = new CheckBox { Text = "Open this app in the tray when I sign in to Windows", AutoSize = true, Location = new Point(lx, y), Checked = S.LaunchAtLogin };
    chkLogin.CheckedChanged += (s2, e) => { if (loading) return; S.LaunchAtLogin = chkLogin.Checked; app.SettingsChanged(false); };
    Controls.Add(chkLogin);
    y += 40;

    btnToggle = new Button { Text = "Start server", Bounds = new Rectangle(lx, y, 130, 32), BackColor = Pal.SageDeep, ForeColor = Pal.Icy, FlatStyle = FlatStyle.Flat, Font = new Font("Segoe UI Semibold", 9.5f) };
    btnToggle.FlatAppearance.BorderSize = 0;
    btnToggle.Click += (s2, e) => { CommitFields(); app.Toggle(); };
    btnRestart = new Button { Text = "Restart", Bounds = new Rectangle(lx + 138, y, 80, 32) };
    btnRestart.Click += (s2, e) => { CommitFields(); app.Restart(); };
    var btnBrowser = new Button { Text = "Browse", Bounds = new Rectangle(lx + 226, y, 64, 32) };
    btnBrowser.Click += (s2, e) => app.OpenBrowser();
    var btnFolder = new Button { Text = "Folder", Bounds = new Rectangle(lx + 296, y, 60, 32) };
    btnFolder.Click += (s2, e) => app.OpenFolder();
    var btnLog = new Button { Text = "Log", Bounds = new Rectangle(lx + 362, y, 46, 32) };
    btnLog.Click += (s2, e) => app.OpenLog();
    Controls.AddRange(new Control[] { btnToggle, btnRestart, btnBrowser, btnFolder, btnLog });
    y += 44;

    var foot = new Label { Text = "Closing this window keeps the app in the tray. Right-click the tray icon to quit.", AutoSize = true, Location = new Point(lx, y), ForeColor = Pal.BrownSoft, Font = new Font("Segoe UI", 8.5f) };
    Controls.Add(foot);
    ClientSize = new Size(440, y + 28);

    // header accent rule + flat secondary buttons in the palette
    Controls.Add(new Panel { Bounds = new Rectangle(0, 72, 440, 3), BackColor = Pal.Sage });
    foreach (Control c in Controls) {
      var b = c as Button;
      if (b == null || b == btnToggle) continue;
      b.FlatStyle = FlatStyle.Flat;
      b.BackColor = Pal.SageMist;
      b.ForeColor = Pal.Charcoal;
      b.FlatAppearance.BorderColor = Pal.Sage;
      b.FlatAppearance.MouseOverBackColor = Color.FromArgb(212, 220, 200);
      b.FlatAppearance.MouseDownBackColor = Pal.Sage;
    }
    foreach (var cb in new[] { chkStart, chkLogin }) cb.ForeColor = Pal.Charcoal;
    txtUrl.TabStop = false;     // don't open with the URL selected in bright blue
    ActiveControl = btnToggle;
    Shown += (s2, e) => { txtUrl.SelectionLength = 0; btnToggle.Focus(); };
  }

  void CommitFields() {
    bool changed = txtUser.Text.Trim() != S.User || txtPass.Text != S.Password || txtFolder.Text.Trim() != S.Folder;
    if (!changed) return;
    S.User = txtUser.Text.Trim(); S.Password = txtPass.Text; S.Folder = txtFolder.Text.Trim();
    app.SettingsChanged(true);  // a running server picks the new values up on restart
  }

  public void Busy(string what) { lblState.Text = what; lblDot.ForeColor = Pal.Busy; Application.DoEvents(); }

  public void ShowState(bool running, string url) {
    loading = true;
    lblDot.ForeColor = running ? Pal.Ok : Pal.Off;
    lblState.Text = running ? "Running on port " + S.Port : "Stopped";
    btnToggle.Text = running ? "Stop server" : "Start server";
    btnToggle.BackColor = running ? Pal.Orange : Pal.SageDeep;
    btnRestart.Enabled = running;
    if (txtUrl.Text != url) txtUrl.Text = url;
    txtUrl.Select(0, 0);
    loading = false;
  }

  protected override void OnFormClosing(FormClosingEventArgs e) {
    if (!reallyClose && e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; CommitFields(); Hide(); return; }
    base.OnFormClosing(e);
  }
  public void ReallyClose() { reallyClose = true; Close(); }
}
