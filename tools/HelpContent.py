"""Compile legacy help HTML into the editor's read-only topic format."""
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
import posixpath
import re
import math
import struct
import tempfile
from urllib.parse import unquote, urlsplit
from HelpStyles import Appearance, inherited, resolve_styles, stylesheet


@dataclass
class Element:
    tag: str
    attrs: dict = field(default_factory=dict)
    children: list = field(default_factory=list)
    appearance: Appearance = field(default_factory=Appearance)


class Document(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.root = Element("document")
        self.stack = [self.root]
        self.feed(text.replace("\r\n", "\n").replace("\r", "\n"))

    def handle_starttag(self, tag, attributes):
        # These optional end tags are frequently omitted in the original help.
        close = {"p": ({"p"}, {"td", "th", "li", "body"}),
                 "li": ({"li"}, {"ul", "ol"}), "tr": ({"tr"}, {"table"}),
                 "td": ({"td", "th"}, {"tr", "table"}), "th": ({"td", "th"}, {"tr", "table"})}
        if tag in {"h1", "h2", "h3", "h4", "h5", "h6", "div", "table", "pre", "ul", "ol", "hr", "blockquote"}:
            for at in range(len(self.stack) - 1, 0, -1):
                if self.stack[at].tag == "p":
                    del self.stack[at:]
                    break
                if self.stack[at].tag in {"td", "th", "li", "body", "table"}:
                    break
        if tag in close:
            targets, boundaries = close[tag]
            for at in range(len(self.stack) - 1, 0, -1):
                if self.stack[at].tag in targets:
                    del self.stack[at:]
                    break
                if self.stack[at].tag in boundaries:
                    break
        node = Element(tag, {key: value if value is not None else "1" if key == "border" else "" for key, value in attributes})
        self.stack[-1].children.append(node)
        if tag not in {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "wbr"}:
            self.stack.append(node)

    def handle_startendtag(self, tag, attributes):
        self.handle_starttag(tag, attributes)
        self.handle_endtag(tag)

    def handle_endtag(self, tag):
        for at in range(len(self.stack) - 1, 0, -1):
            if self.stack[at].tag == tag:
                del self.stack[at:]
                break

    def handle_data(self, data):
        if self.stack[-1].tag == "pre" and not self.stack[-1].children and data.startswith("\n"):
            data = data[1:]
        self.stack[-1].children.append(data)


def elements(node, tag):
    for child in node.children:
        if isinstance(child, Element):
            if child.tag == tag:
                yield child
            yield from elements(child, tag)


def plain(node):
    return "".join(child if isinstance(child, str) else plain(child) for child in node.children)


def reference(topic, target):
    url = urlsplit(target.replace("\\", "/"))
    if url.scheme or url.netloc:
        return target
    path = unquote(url.path)
    path = posixpath.normpath(posixpath.join(posixpath.dirname(topic), path)) if path else topic
    return path + ("#" + unquote(url.fragment) if url.fragment else "")


def anchors(node):
    names = [node.attrs.get("id", "")]
    if node.tag == "a":
        names.append(node.attrs.get("name", ""))
    return [["", "", "", name, node.appearance, 0, 0, []] for name in dict.fromkeys(names) if name]


def marked(block, node):
    block[2][:0] = anchors(node)
    return block


# Block kinds and resolved appearance match ui/help/HelpBook.h. Strings are UTF-8;
# every count and length is a little-endian uint32. No SDK data is converted.
def blocks(children, topic, link="", pre=False, appearance=None):
    result, spans = [], []
    appearance = appearance or Appearance()

    def flush(kind=0, value=0):
        nonlocal spans
        if spans and (pre or any(span[0].strip() or "\xa0" in span[0] or span[2] or span[3] or span[0] == "\n" for span in spans)):
            if not pre:
                spans[0][0] = spans[0][0].lstrip(" \t\r\n")
                spans[-1][0] = spans[-1][0].rstrip(" \t\r\n")
            result.append([kind, value, spans, [], appearance])
        spans = []

    def inline(node, target=link, text_style=appearance):
        if isinstance(node, str):
            text = node if pre else re.sub(r"[ \t\r\n\f]+", " ", node)
            if text:
                spans.append([text, target, "", "", text_style, 0, 0, []])
            return
        if node.tag in {"head", "script", "style", "object", "noscript"}:
            return
        attrs = node.attrs
        text_style = node.appearance
        spans.extend(anchors(node))
        if node.tag == "a":
            if "href" in attrs:
                target = reference(topic, attrs["href"])
                if attrs.get("onclick"):
                    target = "unsupported:" + attrs["onclick"]
        if node.tag == "br":
            spans.append(["\n", "", "", "", text_style, 0, 0, []])
        elif node.tag == "img":
            spans.append([attrs.get("alt", ""), target, reference(topic, attrs.get("src", "")), "", text_style, 0, 0, attrs.get("usemap", "")])
        else:
            for child in node.children:
                inline(child, target, text_style)

    for child in children:
        if isinstance(child, str):
            inline(child)
            continue
        tag = child.tag
        if tag in {"head", "script", "style", "object", "noscript"} or child.appearance.display == "none":
            continue
        if tag in {"p", "div", "center", "blockquote", "body", "html", "section", "dl", "dt", "dd"} or child.appearance.display == "block" and tag != "pre":
            flush()
            nested = blocks(child.children, topic, link, pre, inherited(child.appearance))
            if tag not in {"body", "html"} and (nested or anchors(child)):
                result.append(marked([8, 0, [], nested, child.appearance], child))
            else:
                result.extend(nested)
        elif tag in {"h1", "h2", "h3", "h4", "h5", "h6"}:
            flush()
            inline(child)
            previous = appearance
            appearance = child.appearance
            flush(1, int(tag[1]))
            appearance = previous
        elif tag in {"ul", "ol"}:
            flush()
            start = len(result)
            number = 0
            for item in child.children:
                if isinstance(item, Element) and item.tag == "li":
                    number += 1
                    result.append(marked([3, number if tag == "ol" else 0, [], blocks(item.children, topic, appearance=inherited(item.appearance)), item.appearance], item))
                elif not isinstance(item, str) or item.strip():
                    # The legacy HTML also places explanatory text between li's.
                    result.extend(blocks([item], topic, appearance=inherited(child.appearance)))
            nested = result[start:]
            del result[start:]
            result.append(marked([8, 0, [], nested, child.appearance], child))
        elif tag == "li":
            flush()
            result.append(marked([3, 0, [], blocks(child.children, topic, appearance=inherited(child.appearance)), child.appearance], child))
        elif tag == "pre":
            flush()
            content = blocks(child.children, topic, link, True, child.appearance)
            for block in content:
                block[0] = 2
            if anchors(child):
                if not content:
                    content.append([2, 0, [], [], child.appearance])
                marked(content[0], child)
            result.extend(content)
        elif tag == "hr":
            flush()
            result.append(marked([4, 0, [], [], child.appearance], child))
        elif tag == "table":
            flush()
            rows = []
            def rows_in(node):
                for row in node.children:
                    if isinstance(row, Element) and row.tag == "tr":
                        cells = [marked([7, max(1, min(32, int(cell.attrs.get("colspan", "1") or "1"))), [],
                                  blocks(cell.children, topic, appearance=inherited(cell.appearance)), cell.appearance], cell)
                                 for cell in row.children if isinstance(cell, Element) and cell.tag in {"td", "th"}]
                        if cells:
                            rows.append(marked([6, 0, [], cells, row.appearance], row))
                    elif isinstance(row, Element) and row.tag in {"thead", "tbody", "tfoot"}:
                        rows_in(row)
            rows_in(child)
            if rows:
                # Count columns with the same occupancy rules used by table layout.
                occupied = {}
                columns = 0
                for r, row in enumerate(rows):
                    column = 0
                    for cell in row[3]:
                        while occupied.get(column, 0) > r:
                            column += 1
                        for c in range(column, column + cell[1]):
                            occupied[c] = r + cell[4].rowspan
                        column += cell[1]
                        columns = max(columns, column)
                result.append(marked([5, columns, [], rows, child.appearance], child))
        else:
            inline(child)
    flush()
    return result


def image_areas(node, topic, width, height, diagnostics):
    result = []
    for area in elements(node, "area"):
        if not area.attrs.get("href") or "nohref" in area.attrs:
            continue
        shape = {"rect": 0, "poly": 1, "polygon": 1}.get(area.attrs.get("shape", "rect").lower())
        try:
            points = [float(value) for value in re.split(r"[,\s]+", area.attrs.get("coords", "").strip())]
            valid = width and height and len(points) % 2 == 0 and all(math.isfinite(n) for n in points)
            valid = valid and ((shape == 0 and len(points) == 4 and points[2] > points[0] and points[3] > points[1]) or
                              (shape == 1 and len(points) >= 6))
            if not valid:
                raise ValueError()
            normalized = [value / (width if at % 2 == 0 else height) for at, value in enumerate(points)]
            if any(abs(value) > 10000 for value in normalized):
                raise ValueError()
        except ValueError:
            diagnostics.append({"topic": topic, "coords": area.attrs.get("coords", ""), "reason": "Invalid image map area"})
            continue
        link = reference(topic, area.attrs["href"])
        label = area.attrs.get("alt") or area.attrs.get("title") or posixpath.basename(link)
        result.append((shape, link, label, normalized))
    return result


def compile_help(root, tree, home, decode, diagnostics=None):
    from PIL import Image
    pages = []
    assets = {file.relative_to(root).as_posix().casefold(): file for file in root.rglob("*") if file.is_file()}
    image_sizes = {}
    diagnostics = diagnostics if diagnostics is not None else []
    for file in sorted(root.rglob("*")):
        if file.suffix.lower() not in {".html", ".htm"} or file.name == "VirtoolsDevHelp.html":
            continue
        topic = file.relative_to(root).as_posix()
        document = Document(decode(file.read_bytes()))
        maps = {name.casefold(): node for node in elements(document.root, "map")
                for name in (node.attrs.get("name", ""), node.attrs.get("id", "")) if name}
        rules = []
        for css in elements(document.root, "link"):
            if css.attrs.get("rel", "").lower() == "stylesheet":
                source = assets.get(reference(topic, css.attrs.get("href", "")).casefold())
                if source:
                    rules.extend(stylesheet(decode(source.read_bytes())))
        for css in elements(document.root, "style"):
            rules.extend(stylesheet(plain(css)))
        resolve_styles(document.root, rules)
        title = next(elements(document.root, "title"), None)
        body = next(elements(document.root, "body"), document.root)
        content = blocks(body.children, topic, appearance=inherited(body.appearance))
        if anchors(body):
            content.insert(0, marked([0, 0, [], [], inherited(body.appearance)], body))
        def sizes(items):
            for item in items:
                for span in item[2]:
                    if not span[2]:
                        continue
                    key = span[2].casefold()
                    if key not in image_sizes:
                        source = assets.get(key)
                        try:
                            with Image.open(source) as image:
                                image_sizes[key] = image.size
                        except (OSError, AttributeError):
                            image_sizes[key] = (0, 0)
                            diagnostics.append({"topic": topic, "image": span[2], "reason": "Missing or unreadable source image"})
                    span[5], span[6] = image_sizes[key]
                    usemap = span[7]
                    span[7] = []
                    if usemap:
                        node = maps.get(unquote(usemap.removeprefix("#")).casefold())
                        if node:
                            span[7] = image_areas(node, topic, span[5], span[6], diagnostics)
                        else:
                            diagnostics.append({"topic": topic, "reason": "Missing image map", "map": usemap})
                sizes(item[3])
        sizes(content)
        pages.append((topic, plain(title).strip() if title else file.stem, body.appearance, content))
    stream = tempfile.NamedTemporaryFile(dir=root, prefix="help-", suffix=".tmp", delete=False)
    temporary = Path(stream.name)
    try:
        def integer(value):
            stream.write(struct.pack("<I", value))
        def string(value):
            data = value.encode("utf-8")
            integer(len(data))
            stream.write(data)
        def write_blocks(items):
            integer(len(items))
            for kind, value, spans, children, appearance in items:
                integer(kind)
                integer(value)
                write_appearance(appearance)
                integer(len(spans))
                for text, link, image, anchor, appearance, width, height, areas in spans:
                    for field in (text, link, image, anchor):
                        string(field)
                    write_appearance(appearance)
                    integer(width)
                    integer(height)
                    integer(len(areas))
                    for shape, target, label, points in areas:
                        integer(shape)
                        string(target)
                        string(label)
                        integer(len(points))
                        stream.write(struct.pack("<" + "f" * len(points), *points))
                write_blocks(children)
        def write_appearance(style):
            for value in (style.color, style.background, style.flags, style.align, style.valign, style.floating):
                integer(value)
            values = [style.size, style.width, style.height, *style.margin, *style.padding, *style.border,
                      style.cell_padding, style.cell_spacing]
            stream.write(struct.pack("<" + "f" * len(values), *values))
            integer(style.border_color)
            integer(style.rowspan)
            integer(style.collapse_borders)
            for value in (style.hover_color, style.visited_color, style.hover_flags, style.visited_flags):
                integer(value)
        def contents(nodes):
            integer(len(nodes))
            for node in nodes:
                string(node["name"])
                string(node.get("local", ""))
                contents(node["children"])
        stream.write(b"NDEVHLP3")
        string(home)
        contents(tree)
        integer(len(pages))
        for path, title, appearance, content in pages:
            string(path)
            string(title)
            write_appearance(appearance)
            write_blocks(content)
        stream.close()
        temporary.replace(root / "help.ndh")
    finally:
        stream.close()
        temporary.unlink(missing_ok=True)
    return len(pages)
