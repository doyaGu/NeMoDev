"""Resolve the HTML/CSS presentation used by the supplied reference manual."""
from dataclasses import dataclass, field
import re


@dataclass
class Appearance:
    color: int = 0xff000000
    background: int = 0
    flags: int = 0                 # bold, italic, monospace, underline, preformatted
    align: int = 0                 # left, center, right
    valign: int = 0                # top, middle, bottom
    floating: int = 0              # none, left, right
    size: float = 10.0             # CSS pixels; the reader applies DPI and zoom
    width: float = 0.0             # CSS pixels, negative values are percentages
    height: float = 0.0
    margin: list = field(default_factory=lambda: [0.0] * 4)  # top, right, bottom, left
    padding: list = field(default_factory=lambda: [0.0] * 4)
    border: list = field(default_factory=lambda: [0.0] * 4)
    border_color: int = 0xff000000
    cell_padding: float = 1.0
    cell_spacing: float = 2.0
    rowspan: int = 1
    collapse_borders: bool = False
    display: str = ""
    hover_color: int = 0xff000000
    visited_color: int = 0xff000000
    hover_flags: int = 0
    visited_flags: int = 0


def inherited(style):
    return Appearance(color=style.color, flags=style.flags, size=style.size,
                      align=style.align, valign=style.valign, hover_color=style.hover_color,
                      visited_color=style.visited_color, hover_flags=style.hover_flags,
                      visited_flags=style.visited_flags)


def color(value):
    value = value.strip().lower()
    value = {"black": "#000000", "white": "#ffffff", "red": "#ff0000", "green": "#008000",
             "blue": "#0000ff", "purple": "#800080", "navy": "#000080", "gray": "#808080",
             "silver": "#c0c0c0", "yellow": "#ffff00", "transparent": "#00000000"}.get(value, value)
    if re.fullmatch(r"#[0-9a-f]{3}", value):
        value = "#" + "".join(c * 2 for c in value[1:])
    if re.fullmatch(r"#[0-9a-f]{6}", value):
        r, g, b = (int(value[i:i+2], 16) for i in (1, 3, 5))
        return 0xff000000 | r | g << 8 | b << 16
    return 0 if value == "#00000000" else None


def length(value, size=10.0, percentages=True):
    match = re.fullmatch(r"\s*([+-]?(?:\d*\.)?\d+)\s*(px|pt|em|cm|mm|in|%)?\s*", value, re.I)
    if not match:
        return None
    number = float(match[1])
    unit = (match[2] or "px").lower()
    if unit == "%":
        return -max(0.0, min(100.0, number)) if percentages else number * size / 100.0
    return max(0.0, min(10000.0, number * {"px": 1, "pt": 96 / 72, "em": size,
                                                         "cm": 96 / 2.54, "mm": 96 / 25.4, "in": 96}[unit]))


def declarations(text):
    return {key.strip().lower(): value.strip() for item in text.split(";") if ":" in item
            for key, value in [item.split(":", 1)]}


def stylesheet(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    rules = []
    for selectors, body in re.findall(r"([^{}]+)\{([^{}]*)\}", text):
        for selector in selectors.split(","):
            selector = selector.strip().lower()
            if re.fullmatch(r"(?:[\w-]+)?(?:\.[\w-]+)?(?::(?:link|hover|visited))?", selector) and selector:
                rules.append((selector, declarations(body)))
    return rules


def box(values, size):
    parts = [length(value, size, False) for value in values.split()]
    if not 1 <= len(parts) <= 4 or any(part is None for part in parts):
        return None
    if len(parts) == 1:
        return parts * 4
    if len(parts) == 2:
        return parts * 2
    if len(parts) == 3:
        return [parts[0], parts[1], parts[2], parts[1]]
    return parts


def apply(style, properties):
    for name, value in properties.items():
        lower = value.lower()
        if name in ("color", "background", "background-color", "border-color"):
            parsed = color(lower)
            if parsed is not None:
                setattr(style, {"color": "color", "background": "background", "background-color": "background",
                                "border-color": "border_color"}[name], parsed)
        elif name == "font-size":
            parsed = {"xx-small": 9, "x-small": 10, "small": 13, "medium": 16, "large": 18,
                      "x-large": 24, "xx-large": 32}.get(lower)
            if parsed is None:
                parsed = length(lower, style.size, False)
            if parsed:
                style.size = parsed
        elif name == "font-weight":
            style.flags = style.flags | 1 if lower in ("bold", "bolder", "600", "700", "800", "900") else style.flags & ~1
        elif name == "font-style":
            style.flags = style.flags | 2 if lower in ("italic", "oblique") else style.flags & ~2
        elif name == "font-family":
            if any(word in lower for word in ("courier", "monospace", "consolas")):
                style.flags |= 4
            else:
                style.flags &= ~4
        elif name == "text-decoration":
            style.flags = style.flags | 8 if "underline" in lower else style.flags & ~8
        elif name == "white-space" and lower.startswith("pre"):
            style.flags |= 16
        elif name == "text-align":
            style.align = {"left": 0, "center": 1, "right": 2}.get(lower, style.align)
        elif name == "display":
            style.display = lower
        elif name == "border-collapse":
            style.collapse_borders = lower == "collapse"
        elif name == "vertical-align":
            style.valign = {"top": 0, "middle": 1, "bottom": 2}.get(lower, style.valign)
        elif name in ("width", "height"):
            parsed = length(lower, style.size)
            if parsed is not None:
                setattr(style, name, parsed)
        elif name in ("margin", "padding"):
            parsed = box(lower, style.size)
            if parsed is not None:
                setattr(style, name, parsed)
        elif re.fullmatch(r"(?:margin|padding)-(?:top|right|bottom|left)", name):
            field, side = name.split("-")
            parsed = length(lower, style.size, False)
            if parsed is not None:
                getattr(style, field)[["top", "right", "bottom", "left"].index(side)] = parsed
        elif name == "border" or re.fullmatch(r"border-(?:top|right|bottom|left)", name):
            width = 0.0 if "none" in lower else 1.0
            for part in lower.split():
                parsed = length(part, style.size, False)
                if parsed is not None:
                    width = parsed
                parsed_color = color(part)
                if parsed_color is not None:
                    style.border_color = parsed_color
            if name == "border":
                style.border = [width] * 4
            else:
                style.border[["top", "right", "bottom", "left"].index(name.split("-")[1])] = width


def resolve(node, rules, parent=None, state=""):
    if isinstance(node, str):
        return
    style = Appearance()
    if parent:
        style.color, style.flags, style.size = parent.color, parent.flags, parent.size
        style.align, style.valign = parent.align, parent.valign
        style.cell_padding, style.cell_spacing = parent.cell_padding, parent.cell_spacing
    tag, attrs = node.tag, node.attrs
    if tag == "body":
        style.background = 0xffffffff
        style.margin = [8.0] * 4
    if tag in ("b", "strong", "th"):
        style.flags |= 1
    if tag in ("i", "em"):
        style.flags |= 2
    if tag in ("pre", "code", "tt", "kbd", "samp"):
        style.flags |= 4
    if tag == "pre":
        style.flags |= 16
    if tag == "u" or tag == "a":
        style.flags |= 8
    if tag == "font":
        face = attrs.get("face", "")
        if face:
            apply(style, {"font-family": face})
        value = attrs.get("size", "")
        if re.fullmatch(r"[+-]?\d+", value):
            number = int(value) + (3 if value.startswith(("+", "-")) else 0)
            style.size = [0, 10, 13, 16, 18, 24, 32, 48][max(1, min(7, number))]
    if tag in ("p", "pre", "blockquote", "ul", "ol"):
        style.margin[0] = style.margin[2] = style.size
    if tag in ("blockquote", "ul", "ol"):
        style.margin[3] = 40.0
    if re.fullmatch(r"h[1-6]", tag):
        style.flags |= 1
        style.size = {"h1": 20, "h2": 18, "h3": 16, "h4": 14, "h5": 12, "h6": 10}[tag]
        style.margin[0] = style.margin[2] = style.size * 0.67
    if tag in ("td", "th"):
        style.padding = [style.cell_padding] * 4
    for key in ("cellpadding", "cellspacing"):
        parsed = length(attrs.get(key, ""))
        if parsed is not None:
            setattr(style, {"cellpadding": "cell_padding", "cellspacing": "cell_spacing"}[key], parsed)
    for key in ("width", "height"):
        parsed = length(attrs.get(key, ""))
        if parsed is not None:
            setattr(style, key, parsed)
    for key, field in (("color", "color"), ("bgcolor", "background"), ("bordercolor", "border_color")):
        parsed = color(attrs.get(key, ""))
        if parsed is not None:
            setattr(style, field, parsed)
    if attrs.get("border", "").isdigit():
        style.border = [float(attrs["border"])] * 4
    if tag == "img" and attrs.get("align", "").lower() in ("left", "right"):
        style.floating = 1 if attrs["align"].lower() == "left" else 2
    else:
        style.align = {"left": 0, "center": 1, "right": 2}.get(attrs.get("align", "").lower(), style.align)
    style.valign = {"top": 0, "middle": 1, "bottom": 2}.get(attrs.get("valign", "").lower(), style.valign)
    if tag == "center":
        style.align = 1
    if attrs.get("rowspan", "").isdigit():
        style.rowspan = max(1, min(1000, int(attrs["rowspan"])))
    classes = attrs.get("class", "").lower().split()
    matches = []
    for order, (selector, properties) in enumerate(rules):
        selector, _, pseudo = selector.partition(":")
        if pseudo and pseudo != (state or "link") and not (pseudo == "link" and state == "hover"):
            continue
        match_tag, _, match_class = selector.partition(".")
        if (not match_tag or match_tag == tag) and (not match_class or match_class in classes):
            matches.append((100 * (bool(match_class) + bool(pseudo)) + bool(match_tag), order, properties))
    for _, _, properties in sorted(matches):
        apply(style, properties)
    apply(style, declarations(attrs.get("style", "")))
    if state:
        setattr(node.appearance, state + "_color", style.color)
        setattr(node.appearance, state + "_flags", style.flags)
    else:
        node.appearance = style
    for child in node.children:
        resolve(child, rules, style, state)


def resolve_styles(node, rules):
    # Resolve inheritance for each link state as well: a nested font/span can
    # override the anchor's color. Only text color and decoration vary in the
    # reference's interactive rules; geometry stays independent of navigation.
    for state in ("", "hover", "visited"):
        resolve(node, rules, state=state)
