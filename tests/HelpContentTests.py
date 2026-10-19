"""Exercise the actual HTML compiler and native reader with a small help book."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from ExtractHelp import Contents, correct_topic_case, decode
from HelpContent import Document, blocks, compile_help, reference, image_areas
from HelpStyles import resolve_styles, stylesheet


class HelpContentTests(unittest.TestCase):
    def test_missing_image_is_reported_without_modifying_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = '<body><img src="missing.png" width="11" height="11">Script</body>'
            (root / 'index.html').write_text(source, encoding='utf-8')
            issues = []
            self.assertEqual(compile_help(root, [], 'index.html', decode, issues), 1)
            self.assertEqual(issues, [{'topic': 'index.html', 'image': 'missing.png', 'reason': 'Missing or unreadable source image'}])
            self.assertEqual((root / 'index.html').read_text(encoding='utf-8'), source)

    def test_image_maps_and_alignment(self):
        document = Document('<map name="m"><area shape="rect" coords="0,0,50,40" href="other.html#top">'
                            '<area shape="poly" coords="0,0,100,0,0,80" href="#top">'
                            '<area shape="poly" coords="526,98,480,73" href="#"></map>')
        issues = []
        areas = image_areas(document.root.children[0], 'topics/page.html', 100, 80, issues)
        self.assertEqual(len(areas), 2)
        self.assertEqual(areas[0][1], 'topics/other.html#top')
        self.assertEqual(areas[0][3], [0, 0, .5, .5])
        self.assertEqual(areas[1][3], [0, 0, 1, 0, 0, 1])
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0]['coords'], '526,98,480,73')
        document = Document('<body><img align="bottom"><img align="absmiddle">'
                            '<table><tr><td>A<td valign="top">B</table></body>')
        resolve_styles(document.root, [])
        body = document.root.children[0]
        self.assertEqual([n.appearance.valign for n in body.children[:2]], [2, 1])
        cells = body.children[2].children[0].children
        self.assertEqual([n.appearance.valign for n in cells], [1, 0])

    def test_element_ids_and_named_anchors(self):
        document = Document('<body><h3 id="heading">Title</h3><div id="section">Text</div>'
                            '<table id="table"><tr id="row"><td id="cell">Value</table>'
                            '<img id="diagram" src="a.png"><a id="new" name="old">End</a></body>')
        resolve_styles(document.root, [])
        content = blocks(document.root.children[0].children, 'page.html')
        def names(items):
            for item in items:
                yield from (span[3] for span in item[2] if span[3])
                yield from names(item[3])
        self.assertEqual(set(names(content)), {'heading', 'section', 'table', 'row', 'cell', 'diagram', 'new', 'old'})

    def test_resolved_css_and_html_layout(self):
        document = Document('<body><h3>Inputs</h3><p><span class="pin">Key</span></p>'
                            '<center><img src="a.png" width="120" height="40" border="1"></center>'
                            '<table border="0" width="75%" cellpadding="0" cellspacing="0">'
                            '<tr><td width="20%">A<td rowspan="2" width="80%">B<tr><td>C</table>'
                            '<pre class="code">\r\nfirst\r\n\tsecond</pre></body>')
        resolve_styles(document.root, stylesheet('body {background:white;font-size:x-small} h3 {color:#660066;'
                'background:#f2f2f2;margin-top:24pt;padding-left:5pt;font-size:medium} '
                '.pin {color:#4040ff;font-weight:bold} .code {background:#faebd7}'))
        content = blocks(document.root.children, 'page.html')
        def visit(items):
            for item in items:
                yield item
                yield from visit(item[3])
        all_blocks = list(visit(content))
        heading = next(block for block in all_blocks if block[0] == 1)
        self.assertEqual(heading[4].color, 0xff660066)
        self.assertEqual(heading[4].background, 0xfff2f2f2)
        self.assertEqual(heading[4].margin[0], 32.0)
        self.assertAlmostEqual(heading[4].padding[3], 20 / 3)
        pin = next(span for block in all_blocks for span in block[2] if span[0] == 'Key')
        self.assertEqual(pin[4].color, 0xffff4040)
        self.assertTrue(pin[4].flags & 1)
        image = next(span for block in all_blocks for span in block[2] if span[2])
        self.assertEqual((image[4].width, image[4].height, image[4].align), (120, 40, 1))
        table = next(block for block in all_blocks if block[0] == 5)
        self.assertEqual(table[1], 2)
        self.assertEqual(table[4].width, -75)
        self.assertEqual(table[4].border, [0] * 4)
        self.assertEqual(table[3][0][3][1][4].rowspan, 2)
        self.assertEqual(table[3][0][3][0][4].padding, [0] * 4)
        code = next(block for block in all_blocks if block[0] == 2)
        self.assertEqual(code[2][0][0], 'first\n\tsecond')
        self.assertTrue(code[4].flags & 4)
        self.assertEqual(code[4].background, 0xffd7ebfa)

    def test_link_states_and_optional_paragraph_end(self):
        document = Document('<body><p>Start&nbsp;here<h3>Title</h3><p><a href="page.html">'
                            'Link<span style="color:green">fixed</span></a></body>')
        resolve_styles(document.root, stylesheet('a {text-decoration:none} a:link {color:red}'
                       'a:hover {color:blue;text-decoration:underline} a:visited {color:purple}'))
        body = document.root.children[0]
        self.assertEqual([child.tag for child in body.children], ['p', 'h3', 'p'])
        content = blocks(body.children, 'index.html')
        self.assertEqual(content[0][3][0][2][0][0], 'Start\xa0here')
        spans = content[2][3][0][2]
        link, fixed = (span[4] for span in spans)
        self.assertEqual((link.color, link.hover_color, link.visited_color), (0xff0000ff, 0xffff0000, 0xff800080))
        self.assertEqual((link.flags & 8, link.hover_flags & 8, link.visited_flags & 8), (0, 8, 0))
        self.assertEqual((fixed.color, fixed.hover_color, fixed.visited_color), (0xff008000,) * 3)

    def test_block_styles_and_failed_compile(self):
        document = Document('<body><span class="code">Example</span>'
                            '<table style="border-collapse:collapse"><tr><td>A</table></body>')
        resolve_styles(document.root, stylesheet('.code {display:block;background:#d7ebfa}'))
        content = blocks(document.root.children[0].children, 'index.html')
        self.assertEqual(content[0][0], 8)
        self.assertEqual(content[0][4].background, 0xfffaebd7)
        self.assertTrue(content[1][4].collapse_borders)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            previous = b'previous compiled book'
            (root / 'help.ndh').write_bytes(previous)
            (root / 'index.html').write_text('<body>Content</body>', encoding='utf-8')
            with patch('HelpContent.struct') as binary:
                binary.pack.side_effect = OSError('Write failed')
                with self.assertRaises(OSError):
                    compile_help(root, [], 'index.html', decode)
            self.assertEqual((root / 'help.ndh').read_bytes(), previous)
            self.assertEqual(list(root.glob('help-*.tmp')), [])

    def test_relative_links_and_anchors(self):
        self.assertEqual(reference("concepts/page.html", "../images/a%20b.png"), "images/a b.png")
        self.assertEqual(reference("concepts/page.html", "#Inputs"), "concepts/page.html#Inputs")
        self.assertEqual(reference("page.html", "https://example.com/a"), "https://example.com/a")

    def test_markup(self):
        document = Document('<html><head><script>hidden()</script></head><body>'
                            '<h1><a name="top"></a>Example</h1><p>Use <b>bold</b> and '
                            '<a href="#top">a link</a>.<table><tr><th>Name<th>Value'
                            '<tr><td><p><img src="images/a.png"><td><pre>a\n b</pre></table>'
                            '<ul><li>one<li>two</ul><script>bad()</script></body></html>')
        resolve_styles(document.root, [])
        content = blocks(document.root.children, "page.html")
        def visit(items):
            for item in items:
                yield item
                yield from visit(item[3])
        all_blocks = list(visit(content))
        spans = [span for block in all_blocks for span in block[2]]
        self.assertTrue(any(span[0] == "bold" and span[4].flags & 1 for span in spans))
        self.assertTrue(any(span[1] == "page.html#top" for span in spans))
        self.assertTrue(any(span[3] == "top" for span in spans))
        self.assertTrue(any(span[2] == "images/a.png" for span in spans))
        self.assertTrue(any(block[0] == 5 and len(block[3]) == 2 for block in all_blocks))
        self.assertTrue(all(len(block[3]) == 2 for block in all_blocks if block[0] == 6))
        self.assertTrue(any(block[0] == 2 and block[2][0][0] == "a\n b" for block in all_blocks))
        self.assertEqual(sum(block[0] == 3 for block in all_blocks), 2)
        self.assertNotIn("bad()", "".join(span[0] for span in spans))


def native_fixture(binary, real):
    with tempfile.TemporaryDirectory(prefix="nemodev-help-") as temporary:
        root = Path(temporary)
        (root / "images").mkdir()
        from PIL import Image
        Image.new('RGBA', (100, 80), 'white').save(root / "images" / "Diagram.PNG")
        (root / "index.html").write_text('<html><title>Reference</title><body><h1>Reference</h1>'
                                        '<p>Home <a href="topic%20two.html#inputs">Inputs</a></p></body></html>', encoding="utf-8")
        (root / "topic two.html").write_text('<html><meta charset="utf-8"><title>Key Event BB</title><body>'
                                            '<h3><a name="inputs"></a>Inputs</h3><p><span class="pin">Key:</span> '
                                            'Activates when pressed. Caf\u00e9 \u4e2d\u6587 Para<b>meter</b></p>'
                                            '<img src="images/diagram.png" usemap="#Demo">'
                                            '<map name="Demo"><area shape="rect" coords="0,0,50,40" href="index.html">'
                                            '<area shape="poly" coords="0,0,100,0,0,80" href="index.html"></map>'
                                            '<table><tr><td>On<td>Off</table>'
                                            '</body></html>', encoding="utf-8")
        tree = Contents()
        tree.feed('<ul><li><object type="text/sitemap"><param name="Name" value="Home">'
                  '<param name="Local" value="INDEX.HTML"></object></ul>')
        corrections = correct_topic_case(tree.root, root)
        if len(corrections) != 1:
            raise AssertionError("Contents path case was not corrected")
        compile_help(root, tree.root, "index.html", decode)
        args = [str(binary), str(root)] + ([str(real)] if real else [])
        subprocess.run(args, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--reader", type=Path, required=True)
    parser.add_argument("--content", type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(HelpContentTests))
    if not result.wasSuccessful():
        sys.exit(1)
    native_fixture(args.reader, args.content)
