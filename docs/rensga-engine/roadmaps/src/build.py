"""Rensga Engine roadmap builder.

The roadmaps are data (data_*.py, exported to roadmaps.json); this script renders
them into standalone HTML pages plus an index. Edit the data, rerun:

    python build.py <output_dir>
"""
import html
import json
import os
import sys

import data_core
import data_psp
import data_psx
import data_minor

GROUPS = [
    ("Shared", "One engine, editor and API for every console.", data_core.ROADMAPS),
    ("PSP", "PSP target module: engine, editor and API.", data_psp.ROADMAPS),
    ("PSX", "PSX target module: engine, editor and API.", data_psx.ROADMAPS),
    ("Minor features", "Emulator Play button and the three template projects.", data_minor.ROADMAPS),
]

ICON = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" role="img" aria-label="Pequi">
<path d="M33 14c0-4 1-7 3-9" stroke="#5a4326" stroke-width="2.5" fill="none" stroke-linecap="round"/>
<path d="M35 10c6-6 15-6 19-2-5 5-13 6-19 2z" fill="#4f7d2a"/>
<circle cx="32" cy="37" r="23" fill="#5f8f34" stroke="#3f6420" stroke-width="2"/>
<circle cx="32" cy="38" r="15" fill="#f2a516"/>
<circle cx="32" cy="38" r="15" fill="none" stroke="#d98a0b" stroke-width="1.5"/>
<ellipse cx="27" cy="33" rx="5" ry="3.5" fill="#ffd66b" opacity="0.85"/>
</svg>"""

CSS = """
:root{--bg:#faf9f5;--surface:#ffffff;--surface-1:#f3f1ea;--border:rgba(31,30,29,.14);--border-strong:rgba(31,30,29,.3);
--text:#1f1e1d;--text-2:#5e5d59;--muted:#8a8984;--accent-bg:#e6f1fb;--accent:#185fa5;--good-bg:#eaf3de;--good:#3b6d11;
--warn-bg:#faeeda;--warn:#854f0b;--pequi:#f2a516;--leaf:#5f8f34}
@media (prefers-color-scheme:dark){:root{--bg:#1f1e1d;--surface:#2a2927;--surface-1:#262624;--border:rgba(255,255,255,.12);
--border-strong:rgba(255,255,255,.28);--text:#f1efe8;--text-2:#c2c0b6;--muted:#8f8d86;--accent-bg:#0c447c;--accent:#b5d4f4;
--good-bg:#27500a;--good:#c0dd97;--warn-bg:#633806;--warn:#fac775}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.6 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
a{color:var(--accent)}
.wrap{max-width:820px;margin:0 auto;padding:24px 16px 64px}
.top{display:flex;align-items:center;gap:12px;margin-bottom:20px}
.top svg{width:40px;height:40px;flex:0 0 40px}
.brand{font-weight:600;font-size:18px}
.crumb{font-size:13px;color:var(--text-2)}
h1{font-size:24px;font-weight:600;margin:0 0 6px}
h2{font-size:18px;font-weight:600;margin:32px 0 6px}
.lead{color:var(--text-2);margin:0 0 18px}
.tot{display:flex;gap:12px;margin:0 0 14px;flex-wrap:wrap}
.tc{flex:1;min-width:150px;background:var(--surface-1);border-radius:8px;padding:10px 12px}
.tcl{font-size:12px;color:var(--muted)}
.tcv{font-size:18px;font-weight:600}
.bar{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;margin:0 0 12px}
.leg{display:flex;gap:14px;flex-wrap:wrap;font-size:12px;color:var(--muted)}
.leg span{display:flex;align-items:center;gap:6px}
.dot{width:10px;height:10px;border-radius:50%}
.leg .g1{background:var(--accent)}.leg .g2{background:var(--good)}.leg .g3{background:var(--warn)}
.g1{background:var(--accent-bg);color:var(--accent)}.g2{background:var(--good-bg);color:var(--good)}.g3{background:var(--warn-bg);color:var(--warn)}
button.all{font:inherit;font-size:12.5px;padding:5px 11px;border-radius:8px;border:.5px solid var(--border-strong);background:transparent;color:var(--text);cursor:pointer}
.ph{display:flex;align-items:center;gap:12px;padding:12px 14px;border:.5px solid var(--border);border-radius:12px;background:var(--surface);cursor:pointer;margin-bottom:8px;user-select:none}
.ph:hover{border-color:var(--border-strong)}
.pnum{flex:0 0 30px;height:30px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:600}
.pmeta{flex:1;min-width:0}
.ptitle{font-size:15px;font-weight:600;line-height:1.35}
.psub{font-size:12.5px;color:var(--text-2);margin-top:2px;line-height:1.4}
.est{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:11.5px;color:var(--muted);white-space:nowrap}
.chev{color:var(--muted);transition:transform .18s}
.open .chev{transform:rotate(180deg)}
.body{display:none;padding:2px 14px 20px 56px}
.open+.body{display:block}
.lbl{font-size:11.5px;font-family:ui-monospace,Menlo,Consolas,monospace;color:var(--muted);margin:12px 0 8px}
.pills{display:flex;flex-wrap:wrap;gap:6px}
.pill,.ex{font:inherit;font-size:12.5px;padding:5px 11px;border-radius:999px;cursor:pointer;line-height:1.3}
.pill{border:.5px solid var(--border);background:var(--surface-1);color:var(--text)}
.pill:hover{border-color:var(--border-strong)}
.ex{border:1px dashed var(--border-strong);background:transparent;color:var(--text-2)}
.ex:hover{color:var(--text)}
.note{font-size:13px;color:var(--text-2);line-height:1.6;margin:0 0 4px}
.hint{font-size:12.5px;color:var(--muted);margin:8px 0 16px}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}
.card{display:block;text-decoration:none;color:inherit;border:.5px solid var(--border);border-radius:12px;background:var(--surface);padding:14px 16px}
.card:hover{border-color:var(--border-strong)}
.card b{display:block;font-size:15px;margin-bottom:4px}
.card span{display:block;font-size:13px;color:var(--text-2)}
.card i{display:block;font-style:normal;font-family:ui-monospace,Menlo,Consolas,monospace;font-size:11.5px;color:var(--muted);margin-top:8px}
.doc{border-color:var(--pequi);border-width:1.5px}
#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:var(--text);color:var(--bg);padding:8px 14px;border-radius:8px;font-size:13px;opacity:0;transition:opacity .2s;pointer-events:none}
#toast.on{opacity:1}
"""

JS = """
function tg(el){el.classList.toggle('open')}
function toast(t){var e=document.getElementById('toast');e.textContent=t;e.classList.add('on');clearTimeout(window._t);window._t=setTimeout(function(){e.classList.remove('on')},1600)}
function sp(btn){var t=btn.getAttribute('data-prompt');
 if(typeof sendPrompt==='function'){sendPrompt(t);return}
 if(navigator.clipboard){navigator.clipboard.writeText(t).then(function(){toast('Prompt copied, paste it into Claude')},function(){prompt('Copy this prompt:',t)})}
 else{prompt('Copy this prompt:',t)}}
function expandAll(btn){var hs=document.querySelectorAll('.ph');var open=btn.getAttribute('data-open')!=='1';
 hs.forEach(function(h){h.classList.toggle('open',open)});btn.setAttribute('data-open',open?'1':'0');btn.textContent=open?'Collapse all':'Expand all'}
"""

e = html.escape


def page(title, body, up):
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{e(title)} · Rensga Engine</title>
<link rel="icon" href="{up}rensga-icon.svg" type="image/svg+xml">
<style>{CSS}</style></head>
<body><div class="wrap">
<div class="top">{ICON}<div><div class="brand">Rensga Engine</div><div class="crumb"><a href="{up}index.html">Home</a> · roadmaps and design</div></div></div>
{body}
</div><div id="toast"></div><script>{JS}</script></body></html>
"""


def pills(items, cls):
    return "".join(f'<button class="{cls}" data-prompt="{e(p, quote=True)}" onclick="sp(this)">{e(lbl)}</button>' for lbl, p in items)


def render_roadmap(r):
    stats = "".join(f'<div class="tc"><div class="tcl">{e(k)}</div><div class="tcv">{e(v)}</div></div>' for k, v in r["stats"])
    leg = "".join(f'<span><i class="dot g{i + 1}"></i>{e(n)}</span>' for i, n in enumerate(r["legend"]))
    rows = []
    n = 0
    for ph in r["phases"]:
        g, title, sub, est, note, topics, exercises = ph[:7]
        label = ph[7] if len(ph) > 7 else str(n)
        if len(ph) == 7:
            n += 1
        rows.append(
            f'<div class="ph" onclick="tg(this)"><div class="pnum g{g}">{e(label)}</div><div class="pmeta">'
            f'<div class="ptitle">{e(title)}</div><div class="psub">{e(sub)}</div></div>'
            f'<div class="est">{e(est)}</div><span class="chev" aria-hidden="true">&#9662;</span></div>'
            f'<div class="body"><p class="note">{e(note)}</p>'
            f'<div class="lbl">topics</div><div class="pills">{pills(topics, "pill")}</div>'
            f'<div class="lbl">exercises</div><div class="pills">{pills(exercises, "ex")}</div></div>'
        )
    body = (f'<h1>{e(r["title"])}</h1><p class="lead">{e(r["summary"])}</p>'
            f'<div class="tot">{stats}</div>'
            f'<div class="bar"><div class="leg">{leg}</div><button class="all" onclick="expandAll(this)">Expand all</button></div>'
            f'{"".join(rows)}'
            f'<p class="hint">Click a phase to expand it. Pills copy a ready-made prompt to paste into Claude; dashed pills are exercises.</p>')
    return page(r["title"], body, "../")


def render_index():
    parts = ['<h1>Rensga Engine</h1>',
             '<p class="lead">Open-source engine, editor and API for making games that ship as PSX, PSP and PS2 disc images plus native Windows, Linux and Android builds. '
             'Its icon is a pequi fruit, from the cerrado of Goiás, Brazil.</p>',
             '<h2>Design</h2><div class="cards">'
             '<a class="card doc" href="design-doc/rensga-engine-design-doc.html"><b>Design document</b>'
             '<span>Whole-system design with a data-driven philosophy, linked to every roadmap by phase number.</span><i>HTML · Markdown in design-doc/</i></a>'
             '<a class="card" href="brand/visual-identity.html"><b>Visual identity</b>'
             '<span>Pequi palette, design tokens, logo rules, console target colors and the editor theme.</span><i>HTML · Markdown · CSS tokens in brand/</i></a></div>']
    for name, blurb, roadmaps in GROUPS:
        parts.append(f'<h2>{e(name)}</h2><p class="lead">{e(blurb)}</p><div class="cards">')
        for r in roadmaps:
            total = dict(r["stats"]).get("total", "")
            parts.append(f'<a class="card" href="roadmaps/{r["slug"]}.html"><b>{e(r["title"])}</b>'
                         f'<span>{e(r["summary"])}</span><i>{e(total)} · {len(r["phases"])} phases</i></a>')
        parts.append('</div>')
    parts.append('<p class="hint">Roadmap pages are generated from roadmaps/data/roadmaps.json by roadmaps/src/build.py.</p>')
    return page("Home", "".join(parts), "")


def phase_json(phases):
    out, n = [], 0
    for ph in phases:
        g, t, s, est, note, tp, ex = ph[:7]
        label = ph[7] if len(ph) > 7 else str(n)
        if len(ph) == 7:
            n += 1
        out.append({"number": label, "group": g, "title": t, "subtitle": s, "estimate": est, "note": note,
                    "topics": [{"label": a, "prompt": b} for a, b in tp],
                    "exercises": [{"label": a, "prompt": b} for a, b in ex]})
    return out


def to_json():
    out = []
    for name, _, roadmaps in GROUPS:
        for r in roadmaps:
            out.append({
                "group": name, "slug": r["slug"], "title": r["title"], "summary": r["summary"],
                "stats": [{"label": k, "value": v} for k, v in r["stats"]], "legend": r["legend"],
                "phases": phase_json(r["phases"]),
            })
    return out


def main(out):
    os.makedirs(os.path.join(out, "roadmaps", "data"), exist_ok=True)
    with open(os.path.join(out, "rensga-icon.svg"), "w", encoding="utf-8") as f:
        f.write(ICON)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(render_index())
    count = 0
    for _, _, roadmaps in GROUPS:
        for r in roadmaps:
            with open(os.path.join(out, "roadmaps", f'{r["slug"]}.html'), "w", encoding="utf-8") as f:
                f.write(render_roadmap(r))
            count += 1
    with open(os.path.join(out, "roadmaps", "data", "roadmaps.json"), "w", encoding="utf-8") as f:
        json.dump(to_json(), f, ensure_ascii=False, indent=2)
    print(f"built {count} roadmaps into {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "out")
