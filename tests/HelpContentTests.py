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
from HelpContent import Document, compile_help, reference, image_areas, recover_source_images, elements, anchors, searchable_text, plain as plain_text


class HelpContentTests(unittest.TestCase):
    def test_missing_anchors_use_only_unique_existing_headings(self):
        from HelpContent import recover_source_links
        source = Document('<a href="topic.html#groups">Groups</a><a href="topic.html#absent">Absent</a>'
                          '<a href="topic.html#duplicate">Duplicate</a><a href="topic.html#kept">Existing</a>'
                          '<a href="https://example.com/#absent">External</a>')
        target = Document('<h3 id="old">Groups</h3><h3>Duplicate</h3><h3>Duplicate</h3><p id="Kept">Keep</p>')
        issues, recoveries = [], []
        recover_source_links({'index.html': source, 'topic.html': target}, issues, recoveries)
        links = list(elements(source.root, 'a'))
        self.assertEqual([link.attrs.get('href') for link in links],
                         ['topic.html#groups', None, None, 'topic.html#kept', 'https://example.com/#absent'])
        heading = next(elements(target.root, 'h3'))
        self.assertEqual(heading.attrs['id'], 'old')
        self.assertEqual(list(elements(heading, 'a'))[0].attrs['name'], 'groups')
        self.assertEqual([item['link'] for item in issues], ['topic.html#absent', 'topic.html#duplicate'])
        self.assertEqual(recoveries, [{'topic': 'topic.html', 'anchor': 'groups', 'heading': 'Groups'},
                                      {'topic': 'topic.html', 'anchor': 'kept', 'source_anchor': 'Kept'}])
        self.assertEqual(''.join(link.children[0] for link in links), 'GroupsAbsentDuplicateExistingExternal')
        # Recompiling the repaired DOM neither duplicates anchors nor creates content.
        issues, recoveries = [], []
        recover_source_links({'index.html': source, 'topic.html': target}, issues, recoveries)
        self.assertEqual(issues, [])
        self.assertEqual(recoveries, [])
        self.assertEqual(len(list(elements(heading, 'a'))), 1)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            text = '<body><a href="#groups">Groups</a><a href="#Q">Q</a><h3>Groups</h3></body>'
            (root / 'index.html').write_text(text, encoding='utf-8')
            compile_help(root, [], 'index.html', decode)
            self.assertEqual((root / 'index.html').read_text(encoding='utf-8'), text)

    def test_prepared_html_preserves_markup_and_applies_only_source_repairs(self):
        from HelpContent import recover_source_links
        html = ('<!DOCTYPE HTML><title>Original &amp; title</title><body>\r\n'
                '<P CLASS=original>Unchanged <b>markup</b> &amp; text'
                '<a href="#groups">Groups</a><a href="#Q">Q</a><a href="#legacy">Legacy</a>'
                '<h3>Groups</h3><a name="Legacy"></a></body>')
        document = Document(html)
        recover_source_links({'index.html': document}, [], [])
        prepared = document.prepared_html()
        self.assertIn('<P CLASS=original>Unchanged <b>markup</b> &amp; text', prepared)
        self.assertIn('<a>Q</a>', prepared)
        self.assertIn('<h3><a name="groups"></a>Groups</h3>', prepared)
        self.assertIn('<a name="Legacy"><span id="legacy"></span></a>', prepared)
        self.assertEqual(Document('<p>Untouched &amp; markup').prepared_html(), '<p>Untouched &amp; markup')

    def test_linked_resource_topics_preserve_attachment_content(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'index.html').write_text('<a href="readme.txt">Text</a><a href="picture.png">Image</a>', encoding='utf-8')
            source = b'Original <instructions> & content\r\nSecond line'
            (root / 'readme.txt').write_bytes(source)
            Image.new('RGB', (20, 10), 'red').save(root / 'picture.png')
            recoveries = []
            self.assertEqual(compile_help(root, [], 'index.html', decode, [], recoveries), 3)
            self.assertEqual([(item['topic'], item['kind']) for item in recoveries], [('picture.png', 'image'), ('readme.txt', 'text')])
            self.assertEqual((root / 'readme.txt').read_bytes(), source)
            binary = (root / 'help.ndh').read_bytes()
            self.assertIn(b'Original &lt;instructions&gt; &amp; content', binary)

    def test_malformed_hotspot_label_uses_its_destination_title(self):
        from HelpContent import recover_source_links
        document = Document('<map><area shape="rect" coords="535,18,558,37" href="toolbar/9_sh_links_informations.html" '
                            'alt="Show/Hide Link Informations title="Show/Hide Link Information"></map>')
        target = Document('<title>Show/Hide Link Information</title>')
        recoveries = []
        recover_source_links({'ui/7_schematic/0_schematic_index.html': document,
                              'ui/7_schematic/toolbar/9_sh_links_informations.html': target}, [], recoveries)
        areas = image_areas(document.root, 'ui/7_schematic/0_schematic_index.html', 888, 371, [])
        self.assertEqual(areas[0][2], 'Show/Hide Link Information')
        self.assertEqual(areas[0][1], 'ui/7_schematic/toolbar/9_sh_links_informations.html')
        self.assertEqual(areas[0][3], [535 / 888, 18 / 371, 558 / 888, 37 / 371])
        self.assertEqual(len(recoveries), 1)

    def test_recover_only_known_missing_source_images(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'images').mkdir()
            Image.new('RGB', (17, 18), 'gray').save(root / 'images/active.PNG')
            assets = {'images/active.png': root / 'images/active.PNG'}
            missing = 'file:///G%7C/Documentation/Doc%202.5/Dev/WhitePapers/images/Scene_Activate.jpg'
            html = '<body><img src="' + missing + '" width="9" height="10"><img src="unknown.jpg"></body>'
            page = root / 'whitepapers/SceneManagement.htm'
            page.parent.mkdir()
            page.write_text(html, encoding='utf-8')
            document = Document(html)
            recoveries = []
            recover_source_images(root, 'whitepapers/SceneManagement.htm', document, assets, recoveries)
            images = list(elements(document.root, 'img'))
            self.assertEqual(images[0].attrs, {'src': '../images/active.PNG', 'width': '17', 'height': '18'})
            self.assertEqual(images[1].attrs['src'], 'unknown.jpg')
            self.assertEqual(recoveries, [{'topic': 'whitepapers/SceneManagement.htm', 'image': missing, 'replacement': 'images/active.PNG'}])
            self.assertEqual(page.read_text(encoding='utf-8'), html)
            for topic, sources in [('other.htm', assets), ('whitepapers/SceneManagement.htm', {})]:
                document = Document(html)
                recoveries = []
                recover_source_images(root, topic, document, sources, recoveries)
                self.assertEqual(next(elements(document.root, 'img')).attrs['src'], missing)
                self.assertEqual(recoveries, [])
            # An existing original illustration wins; unrelated screenshots must
            # never be cropped at coordinates belonging to another help book.
            for topic, html, sources in [
                ('whitepapers/SceneManagement.htm', html, {**assets, missing.casefold(): page}),
                ('ui/7_schematic/toolbar/2_schematic_explorer.html', '<img src="../../../images/filter_script.png">',
                 {'images/filter_tree.png': root / 'images/active.PNG'})]:
                recoveries = []
                recover_source_images(root, topic, Document(html), sources, recoveries)
                self.assertEqual(recoveries, [])
            self.assertFalse((root / '_recovered/filter_script.png').exists())

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
        body = document.root.children[0]
        self.assertEqual([n.attrs["align"] for n in body.children[:2]], ["bottom", "absmiddle"])
        cells = body.children[2].children[0].children
        self.assertEqual([n.attrs.get("valign") for n in cells], [None, "top"])

    def test_element_ids_and_named_anchors(self):
        document = Document('<body><h3 id="heading">Title</h3><div id="section">Text</div>'
                            '<table id="table"><tr id="row"><td id="cell">Value</table>'
                            '<img id="diagram" src="a.png"><a id="new" name="old">End</a></body>')
        self.assertEqual({name for node in elements(document.root) for name in anchors(node)},
                         {'heading', 'section', 'table', 'row', 'cell', 'diagram', 'new', 'old'})

    def test_resolved_css_and_html_layout(self):
        document = Document('<body><h3>Inputs</h3><p><span class="pin">Key</span></p>'
                            '<center><img src="a.png" width="120" height="40" border="1"></center>'
                            '<table border="0" width="75%" cellpadding="0" cellspacing="0">'
                            '<tr><td width="20%">A<td rowspan="2" width="80%">B<tr><td>C</table>'
                            '<pre class="code">\r\nfirst\r\n\tsecond</pre></body>')
        css = ('body {background:white;font-size:x-small} h3 {color:#660066;background:#f2f2f2;'
               'margin-top:24pt;padding-left:5pt;font-size:medium} .pin {color:#4040ff;font-weight:bold}'
               '.code {background:#faebd7}')
        source = '<style>' + css + '</style>' + document.source
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'index.html').write_text(source, encoding='utf-8')
            compile_help(root, [], 'index.html', decode)
            self.assertIn(source.encode('utf-8'), (root / 'help.ndh').read_bytes())
        table = next(elements(document.root, 'table'))
        self.assertEqual(table.attrs, {'border':'0', 'width':'75%', 'cellpadding':'0', 'cellspacing':'0'})
        self.assertEqual([node.attrs.get('rowspan') for node in elements(table, 'td')], [None, '2', None])
        self.assertEqual(plain_text(next(elements(document.root, 'pre'))), 'first\n\tsecond')

    def test_link_states_and_optional_paragraph_end(self):
        document = Document('<body><p>Start&nbsp;here<h3>Title</h3><p><a href="page.html">'
                            'Link<span style="color:green">fixed</span></a></body>')
        body = document.root.children[0]
        self.assertEqual([child.tag for child in body.children], ['p', 'h3', 'p'])
        self.assertEqual(searchable_text(body), 'Start\xa0here\nTitle\nLinkfixed\n')
        self.assertEqual(document.prepared_html(), document.source)

    def test_block_styles_and_failed_compile(self):
        document = Document('<body><span class="code">Example</span>'
                            '<table style="border-collapse:collapse"><tr><td>A</table></body>')
        self.assertEqual(document.prepared_html(), document.source)
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
        self.assertEqual(document.prepared_html(), document.source)
        self.assertIn('bold', searchable_text(document.root))
        self.assertNotIn('bad()', searchable_text(document.root))
        self.assertNotIn('hidden()', searchable_text(document.root))
        self.assertTrue(any(node.attrs.get('href') == '#top' for node in elements(document.root, 'a')))
        self.assertTrue(any('top' in anchors(node) for node in elements(document.root)))
        self.assertTrue(any(node.attrs.get('src') == 'images/a.png' for node in elements(document.root, 'img')))
        rows = list(elements(document.root, 'tr'))
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(sum(child.tag in {'td', 'th'} for child in row.children if not isinstance(child, str)) == 2 for row in rows))
        self.assertEqual(plain_text(next(elements(document.root, 'pre'))), 'a\n b')
        self.assertEqual(len(list(elements(document.root, 'li'))), 2)


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
        if real:
            from PIL import Image, ImageChops
            with Image.open(real / 'images/filter_tree.png') as source, Image.open(real / '_recovered/filter_script.png') as recovered:
                difference = ImageChops.difference(source.crop((40, 54, 51, 65)).convert('RGB'), recovered.convert('RGB'))
                if difference.getbbox() is not None:
                    raise AssertionError('Recovered Script icon differs from the original screenshot')


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--reader", type=Path, required=True)
    parser.add_argument("--content", type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(HelpContentTests))
    if not result.wasSuccessful():
        sys.exit(1)
    native_fixture(args.reader, args.content)
