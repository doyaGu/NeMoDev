"""Compile legacy help HTML into the editor's read-only topic format."""
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
import posixpath
import re
import struct
from urllib.parse import unquote, urlsplit


@dataclass
class Element:
    tag: str
    attrs: dict = field(default_factory=dict)
    children: list = field(default_factory=list)


class Document(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.root = Element("document")
        self.stack = [self.root]
        self.feed(text)

    def handle_starttag(self, tag, attributes):
        # These optional end tags are frequently omitted in the original help.
        close = {"p": ({"p"}, {"td", "th", "li", "body"}),
                 "li": ({"li"}, {"ul", "ol"}), "tr": ({"tr"}, {"table"}),
                 "td": ({"td", "th"}, {"tr", "table"}), "th": ({"td", "th"}, {"tr", "table"})}
        if tag in close:
            targets, boundaries = close[tag]
            for at in range(len(self.stack) - 1, 0, -1):
                if self.stack[at].tag in targets:
                    del self.stack[at:]
                    break
                if self.stack[at].tag in boundaries:
                    break
        node = Element(tag, dict(attributes))
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


# Block kinds and span style bits match ui/help/HelpBook.h. Strings are UTF-8;
# every count and length is a little-endian uint32. No SDK data is converted.
def blocks(children, topic, style=0, link="", pre=False):
    result, spans = [], []

    def flush(kind=0, value=0):
        nonlocal spans
        if spans and (pre or any(span[0].strip() or span[2] or span[3] or span[0] == "\n" for span in spans)):
            if not pre:
                spans[0][0] = spans[0][0].lstrip(" \t\r\n")
                spans[-1][0] = spans[-1][0].rstrip(" \t\r\n")
            result.append([kind, value, spans, []])
        spans = []

    def inline(node, inherited=style, target=link):
        if isinstance(node, str):
            text = node if pre else re.sub(r"\s+", " ", node)
            if text:
                spans.append([text, target, "", "", inherited])
            return
        if node.tag in {"head", "script", "style", "object", "noscript"}:
            return
        attrs = node.attrs
        inherited |= {"b": 1, "strong": 1, "i": 2, "em": 2, "code": 4, "tt": 4}.get(node.tag, 0)
        inherited |= {"in": 8, "out": 16, "pin": 32, "pout": 64}.get(attrs.get("class", "").lower(), 0)
        if node.tag == "a":
            if attrs.get("id") or attrs.get("name"):
                spans.append(["", "", "", attrs.get("id") or attrs["name"], 0])
            if "href" in attrs:
                target = reference(topic, attrs["href"])
                if attrs.get("onclick"):
                    target = "unsupported:" + attrs["onclick"]
        if node.tag == "br":
            spans.append(["\n", "", "", "", 0])
        elif node.tag == "img":
            spans.append([attrs.get("alt", ""), target, reference(topic, attrs.get("src", "")), "", 0])
        else:
            for child in node.children:
                inline(child, inherited, target)

    for child in children:
        if isinstance(child, str):
            inline(child)
            continue
        tag = child.tag
        if tag in {"head", "script", "style", "object", "noscript"}:
            continue
        if tag in {"p", "div", "center", "blockquote", "body", "html", "section", "dl", "dt", "dd"}:
            flush()
            result.extend(blocks(child.children, topic, style, link, pre))
        elif tag in {"h1", "h2", "h3", "h4", "h5", "h6"}:
            flush()
            inline(child, style | 1)
            flush(1, int(tag[1]))
        elif tag in {"ul", "ol"}:
            flush()
            number = 0
            for item in child.children:
                if isinstance(item, Element) and item.tag == "li":
                    number += 1
                    result.append([3, number if tag == "ol" else 0, [], blocks(item.children, topic)])
                elif not isinstance(item, str) or item.strip():
                    # The legacy HTML also places explanatory text between li's.
                    result.extend(blocks([item], topic))
        elif tag == "pre":
            flush()
            content = blocks(child.children, topic, style | 4, link, True)
            for block in content:
                block[0] = 2
            result.extend(content)
        elif tag == "hr":
            flush()
            result.append([4, 0, [], []])
        elif tag == "table":
            flush()
            rows = []
            def rows_in(node):
                for row in node.children:
                    if isinstance(row, Element) and row.tag == "tr":
                        cells = [[7, max(1, min(32, int(cell.attrs.get("colspan", "1") or "1"))), [],
                                  blocks(cell.children, topic, 1 if cell.tag == "th" else 0)]
                                 for cell in row.children if isinstance(cell, Element) and cell.tag in {"td", "th"}]
                        if cells:
                            rows.append([6, 0, [], cells])
                    elif isinstance(row, Element) and row.tag in {"thead", "tbody", "tfoot"}:
                        rows_in(row)
            rows_in(child)
            if rows:
                result.append([5, max(sum(cell[1] for cell in row[3]) for row in rows), [], rows])
        else:
            inline(child)
    flush()
    return result


def compile_help(root, tree, home, decode):
    pages = []
    for file in sorted(root.rglob("*")):
        if file.suffix.lower() not in {".html", ".htm"} or file.name == "VirtoolsDevHelp.html":
            continue
        topic = file.relative_to(root).as_posix()
        document = Document(decode(file.read_bytes()))
        title = next(elements(document.root, "title"), None)
        body = next(elements(document.root, "body"), document.root)
        pages.append((topic, plain(title).strip() if title else file.stem, blocks(body.children, topic)))
    with (root / "help.ndh").open("wb") as stream:
        def integer(value):
            stream.write(struct.pack("<I", value))
        def string(value):
            data = value.encode("utf-8")
            integer(len(data))
            stream.write(data)
        def write_blocks(items):
            integer(len(items))
            for kind, value, spans, children in items:
                integer(kind)
                integer(value)
                integer(len(spans))
                for text, link, image, anchor, style in spans:
                    for field in (text, link, image, anchor):
                        string(field)
                    integer(style)
                write_blocks(children)
        def contents(nodes):
            integer(len(nodes))
            for node in nodes:
                string(node["name"])
                string(node.get("local", ""))
                contents(node["children"])
        stream.write(b"NDEVHLP1")
        string(home)
        contents(tree)
        integer(len(pages))
        for path, title, content in pages:
            string(path)
            string(title)
            write_blocks(content)
    return len(pages)
