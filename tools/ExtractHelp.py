"""Extract the shipped CHM and turn its contents tree into browser navigation."""
import argparse
import html
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import struct
import subprocess
from urllib.parse import quote, unquote, urlsplit, urlunsplit
from HelpContent import compile_help


class Contents(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = []
        self.stack = []
        self.item = None

    def handle_starttag(self, tag, attributes):
        values = {key.lower(): value for key, value in attributes}
        if tag == "object":
            self.item = {} if values.get("type", "").lower() == "text/sitemap" else None
        elif tag == "param" and self.item is not None:
            self.item[values.get("name", "").lower()] = values.get("value", "")
        elif tag == "ul":
            if self.stack and self.stack[-1]:
                children = self.stack[-1][-1]["children"]
            else:
                children = self.root
            self.stack.append(children)

    def handle_endtag(self, tag):
        if tag == "object" and self.item is not None:
            if self.item.get("name"):
                self.item["children"] = []
                (self.stack[-1] if self.stack else self.root).append(self.item)
            self.item = None
        elif tag == "ul" and self.stack:
            self.stack.pop()


def decode(data):
    charset = re.search(rb"charset\s*=\s*[\"']?([\w-]+)", data[:4096], re.I)
    return data.decode(charset.group(1).decode("ascii") if charset else "cp1252")


def system_values(path):
    values = {}
    data = path.read_bytes()
    at = 4
    while at + 4 <= len(data):
        kind, length = struct.unpack_from("<HH", data, at)
        at += 4
        if at + length > len(data):
            raise ValueError("Truncated CHM system record")
        if kind in (0, 2, 3):
            values[kind] = decode(data[at:at + length].rstrip(b"\0")).replace("\\", "/")
        at += length
    return values


def navigation(nodes):
    items = []
    for node in nodes:
        name = html.escape(node["name"])
        local = node.get("local", "").replace("\\", "/")
        label = f'<a href="{html.escape(quote(local, safe="/#?:=%"))}" target="topic">{name}</a>' if local else name
        children = navigation(node["children"]) if node["children"] else ""
        items.append(f"<li><details><summary>{label}</summary>{children}</details></li>" if children else f"<li>{label}</li>")
    return "<ul>" + "".join(items) + "</ul>"


def topics(nodes):
    for node in nodes:
        if node.get("local"):
            yield node["local"]
        yield from topics(node["children"])


def correct_topic_case(nodes, root):
    # CHM resolves topic names without case, even when its contents table and
    # archive disagree. Resolve only unique matches to actual extracted files.
    files = {}
    for file in root.rglob("*"):
        if file.is_file():
            name = file.relative_to(root).as_posix()
            files.setdefault(name.casefold(), []).append(name)
    corrected = []
    def visit(items):
        for node in items:
            local = node.get("local", "").replace("\\", "/")
            url = urlsplit(local)
            path = unquote(url.path)
            matches = files.get(path.casefold(), [])
            if not url.scheme and len(matches) == 1 and path != matches[0]:
                node["local"] = urlunsplit(("", "", matches[0], url.query, url.fragment))
                corrected.append({"source": local, "topic": node["local"]})
            visit(node["children"])
    visit(nodes)
    return corrected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--seven-zip", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.seven_zip, "x", str(args.archive), "-o" + str(args.output), "-y", "-bso0", "-bsp0"], check=True)
    values = system_values(args.output / "#SYSTEM")
    contents = args.output / values.get(0, "documentation.hhc")
    tree = Contents()
    tree.feed(decode(contents.read_bytes()))
    corrected = correct_topic_case(tree.root, args.output)
    home = values.get(2, "welcome/0_welcome.html")
    if not (args.output / home).is_file():
        raise ValueError("CHM default topic was not extracted: " + home)
    missing = []
    for local in topics(tree.root):
        url = urlsplit(local.replace("\\", "/"))
        if not url.scheme and not (args.output / unquote(url.path)).is_file():
            missing.append(local)
    # Record missing source topics rather than inventing replacement documentation.
    report = {"home": home, "topics": len(list(topics(tree.root))), "missing_source_topics": sorted(set(missing)),
              "case_corrected_topics": corrected}
    report["source_issues"] = []
    report["compiled_topics"] = compile_help(args.output, tree.root, home, decode, report["source_issues"])
    (args.output / "help-manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    page = """<!doctype html><html lang="en"><meta charset="utf-8">
<title>Virtools Online Reference</title><style>
html,body{margin:0;height:100%;font:14px system-ui,sans-serif}body{display:flex}
nav{width:320px;min-width:220px;resize:horizontal;overflow:auto;padding:12px;border-right:1px solid #bbb}
ul{list-style:none;padding-left:16px}li{margin:5px 0}a{color:#174aa0}summary{cursor:pointer}
iframe{flex:1;height:100%;border:0;min-width:0}h1{font-size:17px}
</style><nav aria-label="Contents"><h1>Virtools Online Reference</h1>"""
    page += navigation(tree.root)
    page += '</nav><iframe name="topic" title="Help topic" src="' + html.escape(quote(home, safe="/")) + '"></iframe></html>'
    (args.output / "VirtoolsDevHelp.html").write_text(page, encoding="utf-8")
    print(f"Extracted help: {report['topics']} contents entries, {len(report['missing_source_topics'])} missing source topics")


if __name__ == "__main__":
    main()
