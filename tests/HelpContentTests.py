"""Exercise the actual HTML compiler and native reader with a small help book."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from ExtractHelp import Contents, correct_topic_case, decode
from HelpContent import Document, blocks, compile_help, reference


class HelpContentTests(unittest.TestCase):
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
        content = blocks(document.root.children, "page.html")
        def visit(items):
            for item in items:
                yield item
                yield from visit(item[3])
        all_blocks = list(visit(content))
        spans = [span for block in all_blocks for span in block[2]]
        self.assertTrue(any(span[0] == "bold" and span[4] & 1 for span in spans))
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
        (root / "images" / "Diagram.PNG").write_bytes(b"image")
        (root / "index.html").write_text('<html><title>Reference</title><body><h1>Reference</h1>'
                                        '<p>Home <a href="topic%20two.html#inputs">Inputs</a></p></body></html>', encoding="utf-8")
        (root / "topic two.html").write_text('<html><meta charset="utf-8"><title>Key Event BB</title><body>'
                                            '<h3><a name="inputs"></a>Inputs</h3><p><span class="pin">Key:</span> '
                                            'Activates when pressed. Caf\u00e9 \u4e2d\u6587 Para<b>meter</b></p>'
                                            '<img src="images/diagram.png"><table><tr><td>On<td>Off</table>'
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
