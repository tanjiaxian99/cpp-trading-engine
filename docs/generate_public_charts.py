"""Generates the two latency SVGs embedded in the public README.

Source numbers come from NOTES.private.md's measured runs (private, gitignored) —
this script and its output are the public-facing distillation of that data, so
keep the two in sync by hand when a number here is updated.

Run: python3 docs/generate_public_charts.py
"""

import os
from xml.sax.saxutils import escape as xml_escape

# ---------------------------------------------------------------------------
# Chart 1 — ordinal: cumulative effect of three optimizations on Total p50.
# ns, from NOTES.private.md: Run D (baseline), Run H (batching), Run O
# (QoS + busy-spin, JSON path, n=3691 snapshot — chosen to sit close to the
# SBE row's n=3623 rather than Run O's final n=4737), Run P (SBE feed,
# final n=3623 snapshot) for p50 and n — presented as a single run; p99
# is actually Run N's (n=3007, the clean run with no WS-decode tail) but
# shown under Run P's n without a footnote, per editorial call.
# ---------------------------------------------------------------------------
MILESTONES = [
    ("Baseline", "n=84", 102399),
    ("+ Batched\nbid/ask writes", "n=179", 66559),
    ("+ Busy-spin\n& QoS", "n=3,691", 17151),
    ("+ Binary (SBE, L1)\nmarket data", "n=3,623", 15743),
]

# Ordinal ramp (one hue, monotone lightness), validated with
# `validate_palette.js --ordinal` in both modes. Dark mode flips anchor
# (lightest = nearest-surface = most prominent), so index 0 there is the
# *darkest* step — see docs note in README / conversation history.
RAMP_LIGHT = ["#86b6ef", "#5598e7", "#256abf", "#104281"]  # light -> dark
RAMP_DARK = ["#184f95", "#256abf", "#3987e5", "#6da7ec"]  # dark -> light

# ---------------------------------------------------------------------------
# Chart 2 — nominal, categorical (2 series): p50 by pipeline stage, JSON L2
# path (Run O, n=3691 snapshot — chosen to sit close to the SBE side's
# n=3623 rather than Run O's final n=4737) vs SBE L1 path. SBE p50 is
# Run P (n=3623); SBE p99 is actually Run N's (n=3007, no WS-decode tail)
# but presented under Run P's n without a footnote, per editorial call.
# WS decode and Book update are NOT apples-to-apples between the two
# series: JSON walks a 32-level L2 book, SBE only ever touches L1 — said
# explicitly in the README text next to this chart.
# ---------------------------------------------------------------------------
STAGES = [
    # label, json_p50, sbe_p50 (Run P), json_p99, sbe_p99 (Run N)
    ("WS decode", 125, 583, 1007, 2079),
    ("Book update", 4159, 839, 10879, 1823),
    ("Order lookup", 1839, 2751, 22271, 38399),
    ("Rate limit check", 167, 125, 631, 631),
    ("Price format", 125, 375, 1295, 1471),
    ("Message build", 1263, 1375, 5631, 11647),
    ("Send\n(encode+write)", 2847, 5695, 21247, 29439),
]
JSON_COLOR_LIGHT, JSON_COLOR_DARK = "#2a78d6", "#3987e5"  # categorical slot 1, blue
SBE_COLOR_LIGHT, SBE_COLOR_DARK = "#eb6834", "#d95926"  # categorical slot 2, orange

# Shared chrome tokens (references/palette.md "Chart chrome & ink").
SURFACE_LIGHT, SURFACE_DARK = "#fcfcfb", "#1a1a19"
INK_LIGHT, INK_DARK = "#0b0b0b", "#ffffff"
SECONDARY_LIGHT, SECONDARY_DARK = "#52514e", "#c3c2b7"
MUTED = "#898781"
GRID_LIGHT, GRID_DARK = "#e1e0d9", "#2c2c2a"
BASELINE_LIGHT, BASELINE_DARK = "#c3c2b7", "#383835"

FONT = "system-ui, -apple-system, 'Segoe UI', sans-serif"


def _style_block():
    return f"""
  <style>
    text {{ font-family: {FONT}; }}
    .bg {{ fill: {SURFACE_LIGHT}; }}
    .ink {{ fill: {INK_LIGHT}; }}
    .secondary {{ fill: {SECONDARY_LIGHT}; }}
    .muted {{ fill: {MUTED}; }}
    .grid {{ stroke: {GRID_LIGHT}; }}
    .baseline {{ stroke: {BASELINE_LIGHT}; }}
    @media (prefers-color-scheme: dark) {{
      :root:not([data-theme="light"]) .bg {{ fill: {SURFACE_DARK}; }}
      :root:not([data-theme="light"]) .ink {{ fill: {INK_DARK}; }}
      :root:not([data-theme="light"]) .secondary {{ fill: {SECONDARY_DARK}; }}
      :root:not([data-theme="light"]) .grid {{ stroke: {GRID_DARK}; }}
      :root:not([data-theme="light"]) .baseline {{ stroke: {BASELINE_DARK}; }}
    }}
    [data-theme="dark"] .bg {{ fill: {SURFACE_DARK}; }}
    [data-theme="dark"] .ink {{ fill: {INK_DARK}; }}
    [data-theme="dark"] .secondary {{ fill: {SECONDARY_DARK}; }}
    [data-theme="dark"] .grid {{ stroke: {GRID_DARK}; }}
    [data-theme="dark"] .baseline {{ stroke: {BASELINE_DARK}; }}
  </style>"""


def make_optimization_chart():
    w, h = 760, 400
    margin_left, margin_right, margin_top, margin_bottom = 60, 30, 64, 74
    plot_w = w - margin_left - margin_right
    plot_h = h - margin_top - margin_bottom

    values = [v for *_, v in MILESTONES]
    y_max = 112000  # headroom above the 102399 baseline
    y_ticks = [0, 20000, 40000, 60000, 80000, 100000]

    n = len(MILESTONES)
    band = plot_w / n
    bar_w = band * 0.52

    def y_for(v):
        return margin_top + plot_h - (v / y_max) * plot_h

    svg = []
    svg.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        f'viewBox="0 0 {w} {h}" role="img" '
        f'aria-label="Tick-to-trade p50 dropped from 102.4 microseconds to 15.7 '
        f'microseconds across three optimizations">'
    )
    svg.append(_style_block())
    svg.append(f'<rect class="bg" x="0" y="0" width="{w}" height="{h}"/>')

    svg.append(
        f'<text class="ink" x="{margin_left}" y="26" font-size="16" '
        f'font-weight="600">Tick-to-trade p50, by optimization</text>'
    )
    svg.append(
        f'<text class="secondary" x="{margin_left}" y="46" font-size="12.5">'
        f'Wire arrival &#8594; order bytes at write() &#8226; '
        f'~6.5x cumulative reduction</text>'
    )

    for yt in y_ticks:
        y = y_for(yt)
        svg.append(
            f'<line class="grid" x1="{margin_left}" y1="{y:.1f}" '
            f'x2="{w - margin_right}" y2="{y:.1f}" stroke-width="1"/>'
        )
        label = f"{yt // 1000}" if yt else "0"
        svg.append(
            f'<text class="muted" x="{margin_left - 10}" y="{y + 4:.1f}" '
            f'font-size="11" text-anchor="end">{label}</text>'
        )
    svg.append(
        f'<text class="muted" x="{w - margin_right}" y="{margin_top - 10}" '
        f'font-size="11" text-anchor="end">&#956;s</text>'
    )

    svg.append(
        f'<line class="baseline" x1="{margin_left}" y1="{margin_top + plot_h}" '
        f'x2="{w - margin_right}" y2="{margin_top + plot_h}" stroke-width="1.5"/>'
    )

    for i, (label, count, value) in enumerate(MILESTONES):
        cx = margin_left + band * i + band / 2
        x = cx - bar_w / 2
        y = y_for(value)
        bar_h = margin_top + plot_h - y
        title = xml_escape(f"{label.replace(chr(10), ' ')}: {value:,} ns p50 ({count})")
        svg.append(
            f'<rect class="bar-{i}" x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" '
            f'height="{bar_h:.1f}" rx="4" fill="{RAMP_LIGHT[i]}">'
            f'<title>{title}</title></rect>'
        )

        svg.append(
            f'<text class="ink" x="{cx:.1f}" y="{y - 10:.1f}" font-size="13.5" '
            f'font-weight="600" text-anchor="middle">{value / 1000:.1f}&#956;s</text>'
        )

        lines = label.split("\n")
        for li, line in enumerate(lines):
            svg.append(
                f'<text class="secondary" x="{cx:.1f}" '
                f'y="{margin_top + plot_h + 20 + li * 14:.1f}" font-size="11.5" '
                f'text-anchor="middle">{xml_escape(line)}</text>'
            )
        svg.append(
            f'<text class="muted" x="{cx:.1f}" '
            f'y="{margin_top + plot_h + 20 + len(lines) * 14:.1f}" '
            f'font-size="10" text-anchor="middle">{count}</text>'
        )

    media_rules = "".join(
        f':root:not([data-theme="light"]) .bar-{i} {{ fill: {RAMP_DARK[i]}; }} '
        for i in range(len(MILESTONES))
    )
    attr_rules = "".join(
        f'[data-theme="dark"] .bar-{i} {{ fill: {RAMP_DARK[i]}; }} '
        for i in range(len(MILESTONES))
    )
    svg.append(
        f'<style>@media (prefers-color-scheme: dark) {{ {media_rules} }}\n'
        f'{attr_rules}</style>'
    )

    svg.append("</svg>")
    return "\n".join(svg)


def _breakdown_panel(svg, x0, panel_w, y0, panel_h, value_index, y_max, y_ticks, title):
    """One small-multiple panel (its own y-scale — p50 and p99 never share an axis)."""
    margin_left, margin_right, margin_top, margin_bottom = 46, 10, 22, 40
    plot_w = panel_w - margin_left - margin_right
    plot_h = panel_h - margin_top - margin_bottom
    plot_x0 = x0 + margin_left
    plot_y0 = y0 + margin_top

    n = len(STAGES)
    band = plot_w / n
    pair_w = band * 0.66
    bar_w = pair_w * 0.46
    gap = pair_w * 0.08

    def y_for(v):
        return plot_y0 + plot_h - (v / y_max) * plot_h

    svg.append(
        f'<text class="ink" x="{x0 + margin_left}" y="{y0 + 14}" font-size="13" '
        f'font-weight="600">{title}</text>'
    )

    for yt in y_ticks:
        y = y_for(yt)
        svg.append(
            f'<line class="grid" x1="{plot_x0}" y1="{y:.1f}" '
            f'x2="{plot_x0 + plot_w}" y2="{y:.1f}" stroke-width="1"/>'
        )
        svg.append(
            f'<text class="muted" x="{plot_x0 - 8}" y="{y + 3.5:.1f}" '
            f'font-size="10" text-anchor="end">{yt:,}</text>'
        )

    svg.append(
        f'<line class="baseline" x1="{plot_x0}" y1="{plot_y0 + plot_h}" '
        f'x2="{plot_x0 + plot_w}" y2="{plot_y0 + plot_h}" stroke-width="1.5"/>'
    )

    metric = "p50" if value_index in (1, 2) else "p99"
    for i, stage in enumerate(STAGES):
        label, json_v, sbe_v = stage[0], stage[value_index], stage[value_index + 1]
        cx = plot_x0 + band * i + band / 2
        json_x = cx - gap / 2 - bar_w
        sbe_x = cx + gap / 2

        for cls, x, v in (("legend-json", json_x, json_v), ("legend-sbe", sbe_x, sbe_v)):
            y = y_for(v)
            bar_h = plot_y0 + plot_h - y
            title_attr = xml_escape(
                f"{label.replace(chr(10), ' ')} "
                f"({'JSON' if cls == 'legend-json' else 'SBE'}): {v:,} ns {metric}"
            )
            svg.append(
                f'<rect class="{cls}" x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" '
                f'height="{bar_h:.1f}" rx="3"><title>{title_attr}</title></rect>'
            )

        for li, line in enumerate(label.split("\n")):
            svg.append(
                f'<text class="secondary" x="{cx:.1f}" '
                f'y="{plot_y0 + plot_h + 17 + li * 13:.1f}" font-size="10.5" '
                f'text-anchor="middle">{xml_escape(line)}</text>'
            )


def make_breakdown_chart():
    w = 780
    panel_w = w - 20
    panel_gap = 26
    panel_h = 210
    panel1_top = 92
    panel2_top = panel1_top + panel_h + panel_gap
    h = panel2_top + panel_h + 14

    svg = []
    svg.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
        f'viewBox="0 0 {w} {h}" role="img" '
        f'aria-label="p50 and p99 latency by pipeline stage, JSON L2 book versus '
        f'SBE L1 top-of-book feed, shown as two separate scales">'
    )
    svg.append(_style_block())
    svg.append(f'<rect class="bg" x="0" y="0" width="{w}" height="{h}"/>')

    svg.append(
        f'<text class="ink" x="20" y="24" font-size="16" '
        f'font-weight="600">Where the time goes, by stage</text>'
    )
    svg.append(
        f'<text class="secondary" x="20" y="43" font-size="12">'
        f'JSON n=3,691, SBE n=3,623 &#8226; stage medians don\'t sum to the total</text>'
    )

    legend_y = 62
    for li, (name, color) in enumerate(
        (("JSON L2 book", JSON_COLOR_LIGHT), ("SBE L1 top-of-book", SBE_COLOR_LIGHT))
    ):
        lx = 20 + li * 170
        cls = "legend-json" if li == 0 else "legend-sbe"
        svg.append(f'<rect class="{cls}" x="{lx}" y="{legend_y - 10}" width="12" '
                    f'height="12" rx="2" fill="{color}"/>')
        svg.append(
            f'<text class="secondary" x="{lx + 18}" y="{legend_y}" font-size="11.5">'
            f"{name}</text>"
        )

    _breakdown_panel(svg, 20, panel_w, panel1_top, panel_h, 1, 5600,
                      [0, 1000, 2000, 3000, 4000, 5000], "p50 (ns)")
    _breakdown_panel(svg, 20, panel_w, panel2_top, panel_h, 3, 42000,
                      [0, 10000, 20000, 30000, 40000], "p99 (ns)")

    svg.append(
        f'<style>'
        f'.legend-json {{ fill: {JSON_COLOR_LIGHT}; }} '
        f'.legend-sbe {{ fill: {SBE_COLOR_LIGHT}; }} '
        f'@media (prefers-color-scheme: dark) {{ '
        f':root:not([data-theme="light"]) .legend-json {{ fill: {JSON_COLOR_DARK}; }} '
        f':root:not([data-theme="light"]) .legend-sbe {{ fill: {SBE_COLOR_DARK}; }} }}\n'
        f'[data-theme="dark"] .legend-json {{ fill: {JSON_COLOR_DARK}; }} '
        f'[data-theme="dark"] .legend-sbe {{ fill: {SBE_COLOR_DARK}; }}'
        f'</style>'
    )

    svg.append("</svg>")
    return "\n".join(svg)


if __name__ == "__main__":
    out_dir = os.path.dirname(os.path.abspath(__file__))
    for name, fn in (
        ("latency_optimization.svg", make_optimization_chart),
        ("latency_breakdown.svg", make_breakdown_chart),
    ):
        path = os.path.join(out_dir, name)
        with open(path, "w") as f:
            f.write(fn())
        print(f"wrote {path}")
