"""
app.py  –  Queue Simulation Tkinter UI
=======================================
Drives simulator_bridge (C++ binary) via subprocess JSON protocol.
All queueing logic stays in C++; this file is pure UI.

Layout
------
  ┌──────────────────────────────────────────────────────────────┐
  │  Header bar                                                   │
  ├──────────────┬───────────────────────────────────────────────┤
  │  Config      │  Tab bar: [Live Simulation] [Scenarios]        │
  │  panel       │  ┌──────────────────────────────────────────┐  │
  │              │  │  Stats row                               │  │
  │  ● Servers   │  │  Server lanes                            │  │
  │  ● Arrival   │  │  Queue row                               │  │
  │  ● Svc min   │  │  Queue-length chart                      │  │
  │  ● Svc max   │  │  Event log                               │  │
  │  ● Sim time  │  └──────────────────────────────────────────┘  │
  │  ● Seed      │                                                 │
  │  [▶ RUN]     │  Scenarios tab: comparison table               │
  └──────────────┴───────────────────────────────────────────────┘
"""

import subprocess, json, sys, os, threading, time, math, queue as _queue
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox

# ── Resolve the C++ bridge binary ───────────────────────────────────────────
SCRIPT_DIR  = Path(__file__).parent.resolve()
BRIDGE_PATH = SCRIPT_DIR / "simulator_bridge"
if not BRIDGE_PATH.exists():
    windows_bridge = BRIDGE_PATH.with_suffix(".exe")
    if windows_bridge.exists():
        BRIDGE_PATH = windows_bridge
    else:
        # Try same dir as script, then cwd
        BRIDGE_PATH = Path("simulator_bridge")

# ── Colour palette ───────────────────────────────────────────────────────────
BG          = "#0f1117"
PANEL_BG    = "#161b25"
CARD_BG     = "#1e2535"
BORDER      = "#2a3347"
AMBER       = "#f5a623"
AMBER_DIM   = "#7a5314"
GREEN       = "#4ade80"
GREEN_DIM   = "#166534"
RED         = "#f87171"
RED_DIM     = "#7f1d1d"
CYAN        = "#67e8f9"
CYAN_DIM    = "#164e63"
WHITE       = "#e2e8f0"
GREY        = "#64748b"
GREY_LIGHT  = "#94a3b8"
SERVING_CLR = GREEN
IDLE_CLR    = GREY

# Server chip colours (cycling)
CHIP_COLORS = [AMBER, GREEN, CYAN, "#c084fc", RED, "#60a5fa", "#f9a8d4", "#86efac"]


# ════════════════════════════════════════════════════════════════════════════
# Bridge helper
# ════════════════════════════════════════════════════════════════════════════

class Bridge:
    """Manages the C++ simulator_bridge subprocess."""

    def __init__(self):
        self._proc = None

    def _ensure(self):
        if self._proc is None or self._proc.poll() is not None:
            self._proc = subprocess.Popen(
                [str(BRIDGE_PATH)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                bufsize=1,
            )

    def _send(self, obj: dict) -> dict:
        self._ensure()
        line = json.dumps(obj, separators=(",", ":")) + "\n"
        self._proc.stdin.write(line)
        self._proc.stdin.flush()
        raw = self._proc.stdout.readline()
        return json.loads(raw)

    def run(self, cfg: dict) -> dict:
        return self._send({"cmd": "run", **cfg})

    def scenarios(self) -> dict:
        return self._send({"cmd": "scenarios"})

    def close(self):
        if self._proc and self._proc.poll() is None:
            try:
                self._proc.stdin.write('{"cmd":"quit"}\n')
                self._proc.stdin.flush()
                self._proc.wait(timeout=2)
            except Exception:
                self._proc.kill()

BRIDGE = Bridge()


# ════════════════════════════════════════════════════════════════════════════
# Tiny canvas-chart widget
# ════════════════════════════════════════════════════════════════════════════

class BarChart(tk.Canvas):
    """Animated bar chart that shows per-tick queue lengths."""

    BAR_W = 4   # pixels per bar
    PAD_L = 38
    PAD_R = 8
    PAD_T = 10
    PAD_B = 28

    def __init__(self, parent, height=120, **kw):
        super().__init__(parent, bg=CARD_BG, highlightthickness=0,
                         height=height, **kw)
        self._data: list[int] = []
        self.bind("<Configure>", lambda e: self._redraw())

    def set_data(self, data: list[int]):
        self._data = data
        self._redraw()

    def _redraw(self):
        self.delete("all")
        w = self.winfo_width()
        h = self.winfo_height()
        if w < 10 or h < 10 or not self._data:
            return

        plot_w = w - self.PAD_L - self.PAD_R
        plot_h = h - self.PAD_T - self.PAD_B

        # How many bars fit?
        max_bars = max(1, plot_w // self.BAR_W)
        data = self._data[-max_bars:]
        n = len(data)
        max_val = max(data) if data else 1
        if max_val == 0:
            max_val = 1

        x0 = self.PAD_L
        y_base = h - self.PAD_B

        # Grid lines
        for step in [0.25, 0.5, 0.75, 1.0]:
            gy = y_base - plot_h * step
            self.create_line(x0, gy, w - self.PAD_R, gy,
                             fill=BORDER, dash=(2, 4))
            lbl = str(int(max_val * step))
            self.create_text(x0 - 4, gy, text=lbl, anchor="e",
                             fill=GREY_LIGHT, font=("Consolas", 7))

        # Baseline
        self.create_line(x0, y_base, w - self.PAD_R, y_base, fill=BORDER)

        # Bars
        bar_w = max(1, plot_w // max(n, 1))
        bar_w = min(bar_w, self.BAR_W * 2)
        for i, v in enumerate(data):
            ratio = v / max_val
            bh = max(1, int(plot_h * ratio))
            bx = x0 + i * (plot_w // n) + 1
            by = y_base - bh
            if ratio < 0.40:
                clr = GREEN
            elif ratio < 0.75:
                clr = AMBER
            else:
                clr = RED
            self.create_rectangle(bx, by, bx + max(2, plot_w // n - 1), y_base,
                                  fill=clr, outline="")

        # X-axis labels
        self.create_text(x0, h - 8, text=f"t-{n}", anchor="w",
                         fill=GREY, font=("Consolas", 7))
        self.create_text(w - self.PAD_R, h - 8, text="now", anchor="e",
                         fill=GREY, font=("Consolas", 7))


# ════════════════════════════════════════════════════════════════════════════
# Main application window
# ════════════════════════════════════════════════════════════════════════════

class App(tk.Tk):

    # Playback speed options (label → ms delay per tick)
    SPEEDS = {"Slow": 300, "Normal": 80, "Fast": 20, "Instant": 0}

    def __init__(self):
        super().__init__()
        self.title("Queue System Simulator")
        self.configure(bg=BG)
        self.minsize(1000, 680)

        # State
        self._running       = False
        self._stop_flag     = threading.Event()
        self._tick_queue    = _queue.Queue()   # C++ result → UI thread
        self._history: list[int] = []
        self._events: list[str]  = []
        self._sim_thread: threading.Thread | None = None

        self._build_ui()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        # Load scenarios in background so tab is pre-populated
        threading.Thread(target=self._load_scenarios, daemon=True).start()

    # ── UI construction ───────────────────────────────────────────────────

    def _build_ui(self):
        # ── Header
        hdr = tk.Frame(self, bg=AMBER, height=4)
        hdr.pack(fill="x", side="top")

        title_bar = tk.Frame(self, bg=PANEL_BG)
        title_bar.pack(fill="x", side="top")
        tk.Label(title_bar, text="  QUEUE SYSTEM SIMULATOR",
                 bg=PANEL_BG, fg=AMBER,
                 font=("Consolas", 14, "bold")).pack(side="left", pady=8)
        tk.Label(title_bar, text="Data Structures · Spring 2026  ",
                 bg=PANEL_BG, fg=GREY,
                 font=("Consolas", 10)).pack(side="right", pady=8)

        # ── Main pane
        main = tk.Frame(self, bg=BG)
        main.pack(fill="both", expand=True)

        # Left config panel
        self._build_config_panel(main)

        # Right content area (notebook)
        right = tk.Frame(main, bg=BG)
        right.pack(side="left", fill="both", expand=True, padx=(0, 10), pady=10)

        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Custom.TNotebook",
                        background=BG, borderwidth=0, tabmargins=[0, 0, 0, 0])
        style.configure("Custom.TNotebook.Tab",
                        background=PANEL_BG, foreground=GREY,
                        font=("Consolas", 10, "bold"),
                        padding=[18, 6], borderwidth=0)
        style.map("Custom.TNotebook.Tab",
                  background=[("selected", CARD_BG)],
                  foreground=[("selected", AMBER)])

        self._nb = ttk.Notebook(right, style="Custom.TNotebook")
        self._nb.pack(fill="both", expand=True)

        sim_tab = tk.Frame(self._nb, bg=BG)
        scen_tab = tk.Frame(self._nb, bg=BG)
        self._nb.add(sim_tab,  text="  ▶  Live Simulation  ")
        self._nb.add(scen_tab, text="  ≡  Scenario Comparison  ")

        self._build_sim_tab(sim_tab)
        self._build_scenarios_tab(scen_tab)

    def _build_config_panel(self, parent):
        panel = tk.Frame(parent, bg=PANEL_BG, width=220)
        panel.pack(side="left", fill="y", padx=10, pady=10)
        panel.pack_propagate(False)

        tk.Label(panel, text="PARAMETERS", bg=PANEL_BG, fg=AMBER,
                 font=("Consolas", 10, "bold")).pack(pady=(18, 10))

        # Separator
        tk.Frame(panel, bg=BORDER, height=1).pack(fill="x", padx=14, pady=2)

        # Config widgets
        self._cfg_vars: dict[str, tk.Variable] = {}

        specs = [
            ("Servers",           "numServers",      2,  1,   8,  "int"),
            ("Arrival rate\n(ticks between)", "arrivalRate", 3,  1,  20,  "int"),
            ("Min service",       "minServiceTime",  2,  1,  10,  "int"),
            ("Max service",       "maxServiceTime",  6,  1,  20,  "int"),
            ("Sim duration",      "simulationTime", 100, 20, 500, "int"),
            ("Random seed\n(0 = random)",   "randomSeed",   42,  0, 9999, "int"),
        ]
        for label, key, default, lo, hi, _ in specs:
            self._add_spinbox(panel, label, key, default, lo, hi)

        tk.Frame(panel, bg=BORDER, height=1).pack(fill="x", padx=14, pady=10)

        # Speed selector
        tk.Label(panel, text="Playback speed", bg=PANEL_BG, fg=GREY_LIGHT,
                 font=("Consolas", 9)).pack(anchor="w", padx=16)
        self._speed_var = tk.StringVar(value="Normal")
        speed_frame = tk.Frame(panel, bg=PANEL_BG)
        speed_frame.pack(fill="x", padx=14, pady=(4, 12))
        for spd in self.SPEEDS:
            rb = tk.Radiobutton(speed_frame, text=spd, variable=self._speed_var,
                                value=spd, bg=PANEL_BG, fg=GREY_LIGHT,
                                selectcolor=CARD_BG, activebackground=PANEL_BG,
                                activeforeground=AMBER,
                                font=("Consolas", 9))
            rb.pack(anchor="w")

        tk.Frame(panel, bg=BORDER, height=1).pack(fill="x", padx=14, pady=2)

        # Run / Stop buttons
        btn_frame = tk.Frame(panel, bg=PANEL_BG)
        btn_frame.pack(pady=14, padx=14, fill="x")

        self._run_btn = tk.Button(
            btn_frame, text="▶  RUN", command=self._start_sim,
            bg=AMBER, fg=BG, font=("Consolas", 11, "bold"),
            relief="flat", cursor="hand2", padx=10, pady=6,
            activebackground="#d4911d", activeforeground=BG)
        self._run_btn.pack(fill="x", pady=(0, 6))

        self._stop_btn = tk.Button(
            btn_frame, text="■  STOP", command=self._stop_sim,
            bg=CARD_BG, fg=RED, font=("Consolas", 11, "bold"),
            relief="flat", cursor="hand2", padx=10, pady=6,
            state="disabled",
            activebackground=RED_DIM, activeforeground=RED)
        self._stop_btn.pack(fill="x")

    def _add_spinbox(self, parent, label, key, default, lo, hi):
        var = tk.IntVar(value=default)
        self._cfg_vars[key] = var

        row = tk.Frame(parent, bg=PANEL_BG)
        row.pack(fill="x", padx=14, pady=3)

        tk.Label(row, text=label, bg=PANEL_BG, fg=GREY_LIGHT,
                 font=("Consolas", 9), justify="left",
                 anchor="w", width=16).pack(side="left")

        sb = tk.Spinbox(row, from_=lo, to=hi, textvariable=var, width=5,
                        bg=CARD_BG, fg=WHITE, insertbackground=WHITE,
                        buttonbackground=BORDER, relief="flat",
                        font=("Consolas", 9), highlightthickness=1,
                        highlightbackground=BORDER)
        sb.pack(side="right")

    def _build_sim_tab(self, parent):
        parent.grid_rowconfigure(4, weight=1)
        parent.grid_columnconfigure(0, weight=1)

        # ── Stats row
        self._stats_frame = tk.Frame(parent, bg=CARD_BG, pady=8)
        self._stats_frame.grid(row=0, column=0, sticky="ew", padx=6, pady=(6, 4))

        self._stat_labels: dict[str, tk.Label] = {}
        stat_defs = [
            ("arrived",   "Arrived",    CYAN),
            ("served",    "Served",     GREEN),
            ("in_queue",  "In Queue",   AMBER),
            ("avg_wait",  "Avg Wait",   RED),
            ("avg_qlen",  "Avg Q Len",  AMBER),
            ("throughput","Throughput", CYAN),
        ]
        for i, (key, lbl, clr) in enumerate(stat_defs):
            col = tk.Frame(self._stats_frame, bg=CARD_BG, padx=16)
            col.pack(side="left")
            tk.Label(col, text=lbl, bg=CARD_BG, fg=GREY,
                     font=("Consolas", 8)).pack()
            vl = tk.Label(col, text="—", bg=CARD_BG, fg=clr,
                          font=("Consolas", 13, "bold"))
            vl.pack()
            self._stat_labels[key] = vl

        # Progress bar
        self._progress_frame = tk.Frame(parent, bg=BG, pady=2)
        self._progress_frame.grid(row=1, column=0, sticky="ew", padx=6)
        self._progress_bar = ttk.Progressbar(self._progress_frame,
                                             orient="horizontal",
                                             mode="determinate", maximum=100)
        self._progress_bar.pack(fill="x")
        self._tick_label = tk.Label(self._progress_frame,
                                    text="Tick 0 / —",
                                    bg=BG, fg=GREY_LIGHT,
                                    font=("Consolas", 8))
        self._tick_label.pack(anchor="e")

        # ── Server lanes
        server_outer = tk.Frame(parent, bg=CARD_BG)
        server_outer.grid(row=2, column=0, sticky="ew", padx=6, pady=4)
        tk.Label(server_outer, text="  SERVERS", bg=CARD_BG, fg=AMBER,
                 font=("Consolas", 9, "bold")).pack(anchor="w", pady=(6, 2))
        self._server_frame = tk.Frame(server_outer, bg=CARD_BG)
        self._server_frame.pack(fill="x", padx=8, pady=(0, 8))
        self._server_widgets: list[dict] = []

        # ── Wait queue row
        queue_outer = tk.Frame(parent, bg=CARD_BG)
        queue_outer.grid(row=3, column=0, sticky="ew", padx=6, pady=4)
        tk.Label(queue_outer, text="  WAIT QUEUE  ▶", bg=CARD_BG, fg=AMBER,
                 font=("Consolas", 9, "bold")).pack(anchor="w", pady=(6, 2))
        self._queue_display = tk.Label(queue_outer, text="(empty)", bg=CARD_BG,
                                       fg=GREY, font=("Consolas", 10),
                                       anchor="w", padx=10)
        self._queue_display.pack(fill="x", pady=(0, 8))

        # ── Bottom: chart + log side by side
        bottom = tk.Frame(parent, bg=BG)
        bottom.grid(row=4, column=0, sticky="nsew", padx=6, pady=4)
        bottom.grid_columnconfigure(0, weight=3)
        bottom.grid_columnconfigure(1, weight=2)
        bottom.grid_rowconfigure(0, weight=1)

        chart_frame = tk.Frame(bottom, bg=CARD_BG)
        chart_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 4))
        tk.Label(chart_frame, text="  QUEUE LENGTH HISTORY",
                 bg=CARD_BG, fg=AMBER, font=("Consolas", 9, "bold")).pack(
                     anchor="w", pady=(6, 2))
        self._chart = BarChart(chart_frame)
        self._chart.pack(fill="both", expand=True, padx=6, pady=(0, 6))

        log_frame = tk.Frame(bottom, bg=CARD_BG)
        log_frame.grid(row=0, column=1, sticky="nsew", padx=(4, 0))
        tk.Label(log_frame, text="  EVENT LOG",
                 bg=CARD_BG, fg=AMBER, font=("Consolas", 9, "bold")).pack(
                     anchor="w", pady=(6, 2))
        self._log_text = tk.Text(log_frame, bg=CARD_BG, fg=GREY_LIGHT,
                                 font=("Consolas", 8), state="disabled",
                                 relief="flat", wrap="word",
                                 insertbackground=WHITE)
        self._log_text.pack(fill="both", expand=True, padx=6, pady=(0, 6))
        # Tag colours for log
        self._log_text.tag_configure("arrive", foreground=CYAN)
        self._log_text.tag_configure("serve",  foreground=AMBER)
        self._log_text.tag_configure("depart", foreground=GREEN)
        self._log_text.tag_configure("tick",   foreground=GREY)

    def _build_scenarios_tab(self, parent):
        tk.Label(parent, text="  SCENARIO COMPARISON",
                 bg=BG, fg=AMBER, font=("Consolas", 12, "bold")).pack(
                     anchor="w", padx=10, pady=(14, 4))
        tk.Label(parent, text="  Pre-defined runs using fixed seed 42 — 200 ticks each",
                 bg=BG, fg=GREY, font=("Consolas", 9)).pack(anchor="w", padx=10)

        # Treeview
        cols = ("name", "servers", "arrived", "served", "avgWait",
                "avgQueue", "maxQueue", "throughput")
        hdrs = ("Scenario", "Srv", "Arrived", "Served",
                "Avg Wait", "Avg Q", "Max Q", "Throughput")
        widths = (200, 40, 65, 65, 80, 70, 60, 90)

        style = ttk.Style()
        style.configure("Scen.Treeview",
                        background=CARD_BG, fieldbackground=CARD_BG,
                        foreground=WHITE, font=("Consolas", 9),
                        rowheight=26, borderwidth=0)
        style.configure("Scen.Treeview.Heading",
                        background=PANEL_BG, foreground=AMBER,
                        font=("Consolas", 9, "bold"), relief="flat")
        style.map("Scen.Treeview", background=[("selected", AMBER_DIM)])

        frm = tk.Frame(parent, bg=BG)
        frm.pack(fill="both", expand=True, padx=10, pady=10)

        self._scen_tree = ttk.Treeview(frm, columns=cols, show="headings",
                                       style="Scen.Treeview")
        for col, hdr, w in zip(cols, hdrs, widths):
            self._scen_tree.heading(col, text=hdr)
            self._scen_tree.column(col, width=w, anchor="center")
        self._scen_tree.column("name", anchor="w")

        vsb = ttk.Scrollbar(frm, orient="vertical",
                            command=self._scen_tree.yview)
        self._scen_tree.configure(yscrollcommand=vsb.set)
        self._scen_tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")

        self._scen_loading = tk.Label(parent, text="Loading…",
                                      bg=BG, fg=GREY, font=("Consolas", 10))
        self._scen_loading.pack()

    # ── Server widgets ────────────────────────────────────────────────────

    def _rebuild_server_lanes(self, n: int):
        for w in self._server_frame.winfo_children():
            w.destroy()
        self._server_widgets.clear()

        for s in range(n):
            row = tk.Frame(self._server_frame, bg=CARD_BG, pady=2)
            row.pack(fill="x")

            lbl = tk.Label(row, text=f"Srv-{s+1}", bg=CARD_BG, fg=AMBER,
                           font=("Consolas", 9, "bold"), width=5)
            lbl.pack(side="left", padx=(0, 6))

            status_dot = tk.Label(row, text="○", bg=CARD_BG, fg=IDLE_CLR,
                                  font=("Consolas", 11))
            status_dot.pack(side="left")

            status_txt = tk.Label(row, text="IDLE", bg=CARD_BG, fg=IDLE_CLR,
                                  font=("Consolas", 9), width=8)
            status_txt.pack(side="left")

            info_lbl = tk.Label(row, text="", bg=CARD_BG, fg=GREY_LIGHT,
                                font=("Consolas", 9), width=22, anchor="w")
            info_lbl.pack(side="left", padx=6)

            # Progress bar (canvas)
            prog_canvas = tk.Canvas(row, bg=CARD_BG, height=12, width=180,
                                    highlightthickness=0)
            prog_canvas.pack(side="left", padx=4)

            pct_lbl = tk.Label(row, text="", bg=CARD_BG, fg=GREY_LIGHT,
                               font=("Consolas", 8), width=5)
            pct_lbl.pack(side="left")

            self._server_widgets.append({
                "status_dot": status_dot,
                "status_txt": status_txt,
                "info":       info_lbl,
                "prog":       prog_canvas,
                "pct":        pct_lbl,
            })

    def _update_server_lane(self, idx: int, busy: bool,
                             customer_id: int = 0, elapsed: int = 0,
                             total: int = 0):
        if idx >= len(self._server_widgets):
            return
        w = self._server_widgets[idx]
        if busy:
            pct = int(100 * elapsed / total) if total else 0
            w["status_dot"].config(text="●", fg=SERVING_CLR)
            w["status_txt"].config(text="SERVING", fg=SERVING_CLR)
            w["info"].config(
                text=f"C#{customer_id}  {elapsed}/{total}t", fg=WHITE)
            w["pct"].config(text=f"{pct}%", fg=GREEN)
            # Draw progress bar
            c = w["prog"]
            c.delete("all")
            W, H = 180, 12
            filled = int(W * pct / 100)
            c.create_rectangle(0, 0, W, H, fill=GREEN_DIM, outline="")
            c.create_rectangle(0, 0, filled, H, fill=GREEN, outline="")
        else:
            w["status_dot"].config(text="○", fg=IDLE_CLR)
            w["status_txt"].config(text="IDLE   ", fg=IDLE_CLR)
            w["info"].config(text="", fg=GREY_LIGHT)
            w["pct"].config(text="", fg=GREY_LIGHT)
            c = w["prog"]
            c.delete("all")
            c.create_rectangle(0, 0, 180, 12, fill=BORDER, outline="")

    # ── Simulation logic ──────────────────────────────────────────────────

    def _get_cfg(self) -> dict:
        return {k: v.get() for k, v in self._cfg_vars.items()}

    def _start_sim(self):
        if self._running:
            return
        cfg = self._get_cfg()

        # Validate min ≤ max service
        if cfg["minServiceTime"] > cfg["maxServiceTime"]:
            messagebox.showerror("Config error",
                                 "Min service time must be ≤ Max service time.")
            return

        # Switch to sim tab
        self._nb.select(0)

        # Rebuild server lanes for this config
        self._rebuild_server_lanes(cfg["numServers"])

        # Reset state
        self._history = []
        self._events  = []
        self._stop_flag.clear()
        self._running = True
        self._run_btn.config(state="disabled")
        self._stop_btn.config(state="normal")
        self._progress_bar["value"] = 0
        self._reset_stats()
        self._chart.set_data([])
        self._log_text.config(state="normal")
        self._log_text.delete("1.0", "end")
        self._log_text.config(state="disabled")

        speed_ms = self.SPEEDS[self._speed_var.get()]
        self._sim_thread = threading.Thread(
            target=self._sim_worker,
            args=(cfg, speed_ms),
            daemon=True)
        self._sim_thread.start()
        self.after(50, self._poll_ticks)

    def _stop_sim(self):
        self._stop_flag.set()

    def _sim_worker(self, cfg: dict, speed_ms: int):
        """
        Runs entirely on a worker thread.
        Because the C++ bridge only offers whole-run results, we replicate
        the tick-by-tick animation in Python using the queue-length history
        from the C++ output and a local server-state replay.
        The C++ Simulator is still the authoritative engine — we just
        re-drive the display tick-by-tick from its output.
        """
        import random

        sim_time = cfg["simulationTime"]
        n_srv    = cfg["numServers"]
        seed     = cfg["randomSeed"]

        # ── Run the full simulation in C++ first
        try:
            result = BRIDGE.run(cfg)
        except Exception as e:
            self._tick_queue.put(("error", str(e)))
            return

        if not result.get("ok"):
            self._tick_queue.put(("error", result.get("error", "Unknown error")))
            return

        history = result.get("queueHistory", [])

        # ── Replay the simulation tick-by-tick to animate the UI.
        #    We use the same RNG parameters so behaviour matches the C++ run.
        rng = random.Random(seed if seed != 0 else None)

        def exp_sample(rate):
            return max(1, int(-rate * math.log(max(rng.random(), 1e-12))))

        def unif_sample(lo, hi):
            return rng.randint(lo, hi)

        arr_rate   = cfg["arrivalRate"]
        min_svc    = cfg["minServiceTime"]
        max_svc    = cfg["maxServiceTime"]

        next_arr   = exp_sample(arr_rate)
        cust_id    = 1

        # Simple FIFO queue
        wait_q: list[tuple[int, int, int]] = []  # (id, arr_tick, svc_dur)
        servers: list[dict] = [
            {"busy": False, "cust_id": 0, "start": 0,
             "end": 0, "dur": 0}
            for _ in range(n_srv)
        ]

        total_arrived = 0
        total_served  = 0
        total_wait    = 0.0
        total_svc     = 0.0
        sum_qlen      = 0.0

        for clock in range(sim_time):
            if self._stop_flag.is_set():
                break

            # 1 Arrivals
            while clock >= next_arr:
                dur = unif_sample(min_svc, max_svc)
                wait_q.append((cust_id, next_arr, dur))
                total_arrived += 1
                self._events.insert(0,
                    f"[T{clock:>3}] ARRIVE  Customer #{cust_id} queued")
                cust_id += 1
                next_arr += exp_sample(arr_rate)

            # 2 Release done servers
            for s in range(n_srv):
                srv = servers[s]
                if srv["busy"] and clock >= srv["end"]:
                    c_id  = srv["cust_id"]
                    wait  = srv["start"] - srv.get("arr_tick", srv["start"])
                    total_served += 1
                    total_wait   += wait
                    total_svc    += srv["dur"]
                    srv["busy"]   = False
                    self._events.insert(0,
                        f"[T{clock:>3}] DEPART  Customer #{c_id} from Srv-{s+1}")

            # 3 Assign queue → free servers
            for s in range(n_srv):
                srv = servers[s]
                if not srv["busy"] and wait_q:
                    c_id, c_arr, c_dur = wait_q.pop(0)
                    srv["busy"]     = True
                    srv["cust_id"]  = c_id
                    srv["start"]    = clock
                    srv["arr_tick"] = c_arr
                    srv["end"]      = clock + c_dur
                    srv["dur"]      = c_dur
                    self._events.insert(0,
                        f"[T{clock:>3}] SERVE   Customer #{c_id} → Srv-{s+1}")

            # Use C++ history for the chart (authoritative)
            q_len = history[clock] if clock < len(history) else len(wait_q)
            sum_qlen += q_len

            avg_wait = total_wait / total_served if total_served else 0
            avg_qlen = sum_qlen / (clock + 1)
            throughput = total_served / (clock + 1)

            snap = {
                "clock":      clock,
                "sim_time":   sim_time,
                "arrived":    total_arrived,
                "served":     total_served,
                "in_queue":   q_len,
                "avg_wait":   avg_wait,
                "avg_qlen":   avg_qlen,
                "throughput": throughput,
                "history":    history[: clock + 1],
                "servers":    [dict(s) for s in servers],
                "events":     self._events[:20],
            }
            self._tick_queue.put(("tick", snap))

            if speed_ms > 0:
                time.sleep(speed_ms / 1000.0)

        # Final summary from C++ (authoritative numbers)
        self._tick_queue.put(("done", result))

    def _poll_ticks(self):
        try:
            while True:
                kind, payload = self._tick_queue.get_nowait()
                if kind == "tick":
                    self._apply_tick(payload)
                elif kind == "done":
                    self._apply_done(payload)
                    return
                elif kind == "error":
                    messagebox.showerror("Simulation error", payload)
                    self._sim_finished()
                    return
        except _queue.Empty:
            pass

        if self._running:
            self.after(30, self._poll_ticks)

    def _apply_tick(self, snap: dict):
        clock    = snap["clock"]
        sim_time = snap["sim_time"]

        # Progress
        pct = int(100 * clock / sim_time)
        self._progress_bar["value"] = pct
        self._tick_label.config(text=f"Tick {clock} / {sim_time}")

        # Stats
        self._stat_labels["arrived"].config(text=str(snap["arrived"]))
        self._stat_labels["served"].config(text=str(snap["served"]))
        self._stat_labels["in_queue"].config(text=str(snap["in_queue"]))
        self._stat_labels["avg_wait"].config(
            text=f"{snap['avg_wait']:.2f}t")
        self._stat_labels["avg_qlen"].config(
            text=f"{snap['avg_qlen']:.2f}")
        self._stat_labels["throughput"].config(
            text=f"{snap['throughput']:.3f}")

        # Chart
        self._chart.set_data(snap["history"])

        # Servers
        for i, srv in enumerate(snap["servers"]):
            if i >= len(self._server_widgets):
                break
            if srv["busy"]:
                elapsed = clock - srv["start"]
                self._update_server_lane(i, True,
                                          srv["cust_id"], elapsed, srv["dur"])
            else:
                self._update_server_lane(i, False)

        # Queue display
        q_len = snap["in_queue"]
        if q_len == 0:
            self._queue_display.config(text="(empty)", fg=GREY)
        else:
            chips = ""
            for ci in range(min(q_len, 20)):
                clr = CHIP_COLORS[ci % len(CHIP_COLORS)]
                chips += f"  ●"
            self._queue_display.config(
                text=f"{'  ●' * min(q_len, 20)}{'  …' if q_len > 20 else ''}  ({q_len} waiting)",
                fg=AMBER)

        # Event log
        self._log_text.config(state="normal")
        self._log_text.delete("1.0", "end")
        for ev in snap["events"]:
            tag = "tick"
            if "ARRIVE" in ev:   tag = "arrive"
            elif "SERVE"  in ev: tag = "serve"
            elif "DEPART" in ev: tag = "depart"
            self._log_text.insert("end", ev + "\n", tag)
        self._log_text.config(state="disabled")

    def _apply_done(self, result: dict):
        self._progress_bar["value"] = 100
        self._tick_label.config(text="Complete ✓")

        # Update stats with authoritative C++ numbers
        self._stat_labels["arrived"].config(text=str(result.get("totalCustomers", "—")))
        self._stat_labels["served"].config(text=str(result.get("customersServed", "—")))
        self._stat_labels["avg_wait"].config(
            text=f"{result.get('avgWaitingTime', 0):.2f}t")
        self._stat_labels["avg_qlen"].config(
            text=f"{result.get('avgQueueLength', 0):.2f}")
        self._stat_labels["throughput"].config(
            text=f"{result.get('throughput', 0):.3f}")
        self._stat_labels["in_queue"].config(text="0")

        # Final chart
        history = result.get("queueHistory", [])
        self._chart.set_data(history)

        self._sim_finished()

    def _sim_finished(self):
        self._running = False
        self._run_btn.config(state="normal")
        self._stop_btn.config(state="disabled")

    def _reset_stats(self):
        for lbl in self._stat_labels.values():
            lbl.config(text="—")

    # ── Scenarios tab ─────────────────────────────────────────────────────

    def _load_scenarios(self):
        try:
            result = BRIDGE.scenarios()
        except Exception as e:
            self.after(0, lambda: self._scen_loading.config(
                text=f"Error: {e}", fg=RED))
            return

        if not result.get("ok"):
            self.after(0, lambda: self._scen_loading.config(
                text=result.get("error", "Failed"), fg=RED))
            return

        scenarios = result.get("scenarios", [])
        self.after(0, lambda: self._populate_scenarios(scenarios))

    def _populate_scenarios(self, scenarios: list):
        self._scen_loading.config(text="")
        for i, sc in enumerate(scenarios):
            aw   = sc.get("avgWaitingTime", 0)
            aq   = sc.get("avgQueueLength", 0)
            th   = sc.get("throughput", 0)
            tot  = sc.get("totalCustomers", 0)
            srvd = sc.get("customersServed", 0)
            mq   = sc.get("maxQueueLength", 0)

            pct = srvd / tot if tot else 0
            tag = "good" if aw < 3 else ("warn" if aw < 10 else "bad")

            self._scen_tree.insert("", "end", iid=str(i), tags=(tag,),
                values=(
                    sc.get("name", ""),
                    sc.get("numServers", ""),
                    tot,
                    srvd,
                    f"{aw:.2f}t",
                    f"{aq:.2f}",
                    mq,
                    f"{th:.4f}",
                ))

        self._scen_tree.tag_configure("good", foreground=GREEN)
        self._scen_tree.tag_configure("warn", foreground=AMBER)
        self._scen_tree.tag_configure("bad",  foreground=RED)

    # ── Lifecycle ─────────────────────────────────────────────────────────

    def _on_close(self):
        self._stop_flag.set()
        BRIDGE.close()
        self.destroy()


# ════════════════════════════════════════════════════════════════════════════
# Entry point
# ════════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    if not BRIDGE_PATH.exists():
        print(f"ERROR: Could not find simulator_bridge at {BRIDGE_PATH}", file=sys.stderr)
        print("Build it with:  g++ -std=c++17 -O2 -o simulator_bridge simulator_bridge.cpp",
              file=sys.stderr)
        sys.exit(1)

    app = App()
    app.mainloop()
