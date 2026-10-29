# Rensga visual identity: palette, tokens, logo, type and usage rules.
# build_brand.py renders it to brand/ (HTML guide, Markdown guide, palette.json,
# rensga-tokens.css, one-color icons) and fails if a contrast check does not pass.
#
# Colors are referenced as "<ramp>.<step>" (for example "pequi.500") or as raw hex.
# Steps marked in LOGO_PARTS are taken from the icon (ICON in build.py) and must
# stay identical to it; the build checks this.

INTRO = ("Rensga's look comes from one object: the pequi, a fruit of the cerrado in Goiás, Brazil, "
         "with a green husk around bright golden pulp. The palette takes its main colors straight from the icon, "
         "adds warm neutrals to set them on, and borrows a few more cerrado colors for status messages and console targets.")

PRINCIPLES = [
    ("Gold is the signal.", "Pequi gold marks the one thing to look at: the Play button, the selection, the primary action. Most of a screen is warm neutral."),
    ("Green is the frame.", "Cerrado green carries links, section headers and success."),
    ("Warm, not grey.", "Neutrals are tinted toward soil so gold and green sit naturally on them."),
    ("Gold is a fill on light backgrounds.", "Gold text on white is only 2.1:1. Put dark ink text on a gold fill instead. On dark backgrounds gold text is fine."),
    ("Status is never color alone.", "Pair every status color with an icon or a word."),
    ("Contrast is checked at build time.", "Like a scene over budget, a token pair under its contrast minimum stops build_brand.py with an error that names the pair."),
]

RAMPS = [
    {"key": "pequi", "name": "Pequi gold", "source": "the pulp",
     "use": "Primary actions, Play, selection, focus, highlights, warnings",
     "steps": [(50, "#fff8e6"), (100, "#ffefc2"), (200, "#ffe08a"), (300, "#ffd66b"), (400, "#f7bc3a"),
               (500, "#f2a516"), (600, "#d98a0b"), (700, "#b06d08"), (800, "#85510a"), (900, "#5c380b")]},
    {"key": "cerrado", "name": "Cerrado green", "source": "the husk and the leaf",
     "use": "Links, section headers, success, foliage in art",
     "steps": [(50, "#f1f6ea"), (100, "#e6f0da"), (200, "#c2dba6"), (300, "#9fc477"), (400, "#7eaa50"),
               (500, "#5f8f34"), (600, "#4f7d2a"), (700, "#3f6420"), (800, "#2f4b18"), (900, "#1f3210")]},
    {"key": "soil", "name": "Soil", "source": "warm neutrals",
     "use": "Backgrounds, surfaces, borders, text",
     "steps": [(50, "#faf8f3"), (100, "#f2efe6"), (200, "#e4dfd2"), (300, "#cdc6b5"), (400, "#a59d8a"),
               (500, "#736b5a"), (600, "#5c5547"), (700, "#403b31"), (800, "#2b2822"), (900, "#1c1a16")]},
    {"key": "bark", "name": "Bark", "source": "the stem",
     "use": "Rare accent for wood and earth in art and illustrations; not for UI state",
     "steps": [(300, "#b59a73"), (500, "#7a5c36"), (700, "#5a4326"), (900, "#2e2214")]},
    {"key": "urucum", "name": "Urucum red", "source": "annatto, the red seed dye of Brazil",
     "use": "Errors, Stop, over-budget",
     "steps": [(100, "#fde8e4"), (300, "#f7b4aa"), (700, "#a3261c"), (900, "#5a1a14")]},
    {"key": "ceu", "name": "Cerrado sky", "source": "the dry-season sky",
     "use": "Information and neutral notices",
     "steps": [(100, "#e3eef8"), (300, "#a9cdee"), (700, "#245a8a"), (900, "#173a5c")]},
]

# Icon part -> palette color. Must match the hex values in build.ICON.
LOGO_PARTS = [
    ("Stem", "bark.700"),
    ("Leaf", "cerrado.600"),
    ("Husk", "cerrado.500"),
    ("Husk rim", "cerrado.700"),
    ("Pulp", "pequi.500"),
    ("Pulp rim", "pequi.600"),
    ("Highlight", "pequi.300"),
]

# Console target colors: identify a target in the editor and docs. White text on all of them.
TARGETS = [
    ("psx", "PSX", "Ipê roxo", "#6e4a9e", "the purple ipê tree"),
    ("psp", "PSP", "Vereda", "#1d6f73", "the teal water of the cerrado's palm wetlands"),
    ("ps2", "PS2", "Ipê rosa", "#b23c6e", "the pink ipê tree"),
    ("pc", "Windows · Linux · Android", "Soil 600", "soil.600", "the neutral ramp"),
]

# Semantic tokens: (name, light, dark, use). Use these in UI code, never raw ramp steps.
TOKENS = [
    ("bg", "soil.50", "soil.900", "Page and editor window background"),
    ("surface", "#ffffff", "soil.800", "Cards, panels, inspector rows, inputs"),
    ("surface-2", "soil.100", "#23211c", "Inset areas, code blocks, table headers"),
    ("border", "soil.200", "soil.700", "Dividers and outlines"),
    ("border-strong", "soil.300", "soil.600", "Hovered outlines, input borders"),
    ("text", "soil.900", "soil.50", "Body text and icons"),
    ("text-2", "soil.600", "soil.300", "Secondary text"),
    ("muted", "soil.500", "soil.400", "Captions, hints, placeholders"),
    ("link", "cerrado.700", "cerrado.300", "Links and text buttons"),
    ("primary", "pequi.500", "pequi.500", "Primary button, Play, selection fill"),
    ("primary-hover", "pequi.400", "pequi.400", "Hovered primary button"),
    ("on-primary", "soil.900", "soil.900", "Text and icons on primary"),
    ("focus", "pequi.700", "pequi.500", "Keyboard focus ring (2 px)"),
    ("ok", "cerrado.800", "cerrado.200", "Success text and icons"),
    ("ok-bg", "cerrado.100", "cerrado.900", "Success background"),
    ("warn", "pequi.800", "pequi.300", "Warning text and icons, near-budget"),
    ("warn-bg", "pequi.50", "pequi.900", "Warning background"),
    ("danger", "urucum.700", "urucum.300", "Error text and icons, over-budget, Stop"),
    ("danger-bg", "urucum.100", "urucum.900", "Error background"),
    ("info", "ceu.700", "ceu.300", "Information text and icons"),
    ("info-bg", "ceu.100", "ceu.900", "Information background"),
]

# Contrast checks, run in both themes: (foreground token, background token, minimum ratio).
# 4.5 is WCAG AA for body text; 3.0 is the minimum for UI parts such as focus rings.
CHECKS = [
    ("text", "bg", 4.5), ("text", "surface", 4.5), ("text", "surface-2", 4.5),
    ("text-2", "bg", 4.5), ("text-2", "surface", 4.5),
    ("muted", "bg", 4.5), ("muted", "surface", 4.5),
    ("link", "bg", 4.5), ("link", "surface", 4.5),
    ("on-primary", "primary", 4.5), ("on-primary", "primary-hover", 4.5),
    ("focus", "bg", 3.0), ("focus", "surface", 3.0),
    ("ok", "ok-bg", 4.5), ("warn", "warn-bg", 4.5), ("danger", "danger-bg", 4.5), ("info", "info-bg", 4.5),
]

LOGO = {
    "geometry": "Drawn on a 64x64 grid: husk circle r23 at (32, 37), pulp circle r15 at (32, 38), stem and leaf at the top right.",
    "clear_space": "Keep clear space of at least a quarter of the icon's width on every side.",
    "sizes": [("16 px", "smallest use: favicon"), ("24 px", "editor toolbar and tabs"),
              ("40 px", "docs page header"), ("128 px and up", "splash screen, release pages")],
    "lockup": "Icon on the left, wordmark on the right. Icon height is 2.2x the wordmark font size; the gap is 0.3x the icon height. "
              "The wordmark is \"Rensga Engine\" in the UI font at weight 600, in the text color. The docs header uses 40 px, 18 px and 12 px.",
    # (name, file under the docs root, use, one-color ink or None, backgrounds shown in the guide)
    "variants": [("Full color", "rensga-icon.svg", "Default, on light or dark soil backgrounds", None, ["soil.50", "soil.900"]),
                 ("One color, dark", "brand/rensga-icon-mono-dark.svg", "One-ink printing, engraving, light photos", "soil.900", ["pequi.100"]),
                 ("One color, light", "brand/rensga-icon-mono-light.svg", "Dark photos and solid dark or colored backgrounds", "#ffffff", ["ceu.700"])],
    "do": ["Use the full-color icon on soil backgrounds, light or dark.",
           "Use a one-color version when only one ink is available or the background is busy.",
           "Scale it proportionally and keep its clear space."],
    "dont": ["Recolor its parts, or swap the gold for another hue.",
             "Stretch, rotate, outline it, or add shadows or gradients.",
             "Place the full-color icon on mid-green or gold backgrounds, where the husk or pulp disappears.",
             "Combine it with Sony marks: the PlayStation logo, its four colors, or the controller face-button shapes."],
}

TYPE = {
    "docs_ui": 'system-ui, -apple-system, "Segoe UI", Roboto, sans-serif',
    "docs_mono": "ui-monospace, Menlo, Consolas, monospace",
    "docs_note": "Docs pages use system fonts so they work offline with nothing to download.",
    "editor_note": "Proposed for the editor: Inter for UI text and JetBrains Mono for code and the Console, both SIL OFL 1.1, "
                   "bundled as TTF files because Dear ImGui loads fonts from disk.",
    "scale": [("Page title", 24, 600), ("Section heading", 18, 600), ("Body", 15, 400),
              ("Small", 13, 400), ("Caption", 12.5, 400), ("Label (mono)", 11.5, 400)],
}

# Dear ImGui style mapping for the editor's dark theme: (ImGuiCol_ name, color, carries text).
# Entries that carry text are checked against the dark "text" token at 4.5:1.
EDITOR = [
    ("WindowBg", "bg", True), ("ChildBg", "surface-2", True), ("PopupBg", "surface", True),
    ("Border", "border", False), ("FrameBg", "surface", True), ("FrameBgHovered", "soil.700", True),
    ("FrameBgActive", "soil.600", True), ("TitleBg", "soil.900", True), ("TitleBgActive", "soil.800", True),
    ("Text", "text", False), ("TextDisabled", "muted", False),
    ("Button", "soil.700", True), ("ButtonHovered", "soil.600", True), ("ButtonActive", "pequi.800", True),
    ("Header", "cerrado.800", True), ("HeaderHovered", "cerrado.700", True), ("HeaderActive", "cerrado.600", False),
    ("CheckMark", "primary", False), ("SliderGrab", "pequi.600", False), ("SliderGrabActive", "primary", False),
    ("Tab", "surface-2", True), ("TabHovered", "soil.700", True),
    ("PlotHistogram", "primary", False), ("DockingPreview", "primary", False), ("TextSelectedBg", "ceu.700", True),
]

EDITOR_NOTES = [
    "The editor defaults to the dark theme. A light theme uses the same mapping with the light token values.",
    "The Play button is primary (gold with dark ink). Stop uses danger. Only one gold button is visible at a time.",
    "Budget bars: under 80% uses ok, 80 to 100% uses warn, over 100% uses danger with the asset named. The build fails over 100% anyway.",
    "DockingPreview and TextSelectedBg are drawn at about 40% opacity.",
]

BUDGET_EXAMPLE = [("RAM", 62, "ok"), ("VRAM", 91, "warn"), ("SPU RAM", 104, "danger")]

IMAGERY = [
    "Show game screenshots at the target's native resolution, scaled by whole numbers with nearest-neighbor filtering (PSX 320x240, PSP 480x272, PS2 640x448).",
    "Label every screenshot with its target and whether it came from an emulator or real hardware.",
    "Prefer the template projects' primitive characters and test levels for docs art, so images match what users will see.",
    "In diagrams, draw boxes and lines in soil, use gold for the one path the diagram is about, green for data, and target colors for per-console boxes.",
]

VOICE = [
    "Write \"Rensga Engine\" on first mention, then \"Rensga\".",
    "Plain sentences, in the same tone as the design document.",
    "Say a build \"runs on PSP\". Never suggest that Sony made or endorses Rensga.",
]
