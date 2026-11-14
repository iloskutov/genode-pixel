#!/usr/bin/env python3
#
# \brief  Tests of mixer_paths_to_hid.py
# \author Ivan Loskutov
# \date   2026-10-01
#
# SPDX-License-Identifier: MIT
#
"""Tests for mixer_paths_to_hid.py: python3 tool/test_mixer_paths_to_hid.py"""
import os, shutil, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mixer_paths_to_hid as m                                          # noqa: E402

TOOL    = os.path.join(HERE, "mixer_paths_to_hid.py")
FIXTURE = os.path.join(HERE, "test_data", "mixer_paths.xml")
HID     = os.path.normpath(os.path.join(HERE, "..", "..", "..", "tool", "hid"))


def read(file, mode="r"):
    with open(file, mode) as fd:
        return fd.read()


def table(body, mixer_attr=""):
    return m.parse("<mixer%s>%s</mixer>" % (mixer_attr, body))


def path(t, name):
    """writes of the path as (control, index or None, value)"""
    return m.writes(t.path(name).settings)


def has_warning(t, text):
    return any(text in w for w in t.warnings)


class Defaults(unittest.TestCase):

    def test_order_of_first_occurrence_last_value_wins(self):
        t = table('<ctl name="A" value="1"/><ctl name="B" value="x"/>'
                  '<ctl name="A" value="2"/>')
        self.assertEqual(m.writes(t.defaults), [("A", None, "2"), ("B", None, "x")])

    def test_id_sets_one_element_only(self):
        t = table('<ctl name="L" id="2" value="7"/><ctl name="L" id="1" value="5"/>')
        self.assertEqual(m.writes(t.defaults), [("L", 1, "5"), ("L", 2, "7")])

    def test_whole_control_write_after_ids_replaces_them(self):
        t = table('<ctl name="L" id="1" value="5"/><ctl name="L" value="9"/>')
        self.assertEqual(m.writes(t.defaults), [("L", None, "9")])

    def test_id_after_whole_control_write(self):
        t = table('<ctl name="L" value="9"/><ctl name="L" id="1" value="5"/>')
        self.assertEqual(m.writes(t.defaults), [("L", None, "9"), ("L", 1, "5")])

    def test_defaults_after_paths_still_count(self):
        t = table('<path name="p"><ctl name="A" value="1"/></path>'
                  '<ctl name="Z" value="0"/>')
        self.assertEqual(m.writes(t.defaults), [("Z", None, "0")])
        self.assertEqual(path(t, "p"), [("A", None, "1")])


class Paths(unittest.TestCase):

    def test_controls_in_listed_order(self):
        t = table('<path name="p"><ctl name="B" value="1"/><ctl name="A" value="2"/>'
                  '<ctl name="C" value="ASP"/></path>')
        self.assertEqual(path(t, "p"), [("B", None, "1"), ("A", None, "2"),
                                        ("C", None, "ASP")])

    def test_paths_in_listed_order_and_empty_path(self):
        t = table('<path name="b"></path><path name="a"><ctl name="X" value="1"/></path>')
        self.assertEqual([p.name for p in t.paths], ["b", "a"])
        self.assertEqual(path(t, "b"), [])

    def test_repeated_control_updates_in_place(self):
        t = table('<path name="p"><ctl name="A" value="1"/><ctl name="B" value="1"/>'
                  '<ctl name="A" value="2"/></path>')
        self.assertEqual(path(t, "p"), [("A", None, "2"), ("B", None, "1")])

    def test_unknown_path_is_none(self):
        self.assertIsNone(table("").path("nope"))


class Nested(unittest.TestCase):

    def test_flattened_in_order(self):
        t = table('<path name="a"><ctl name="A" value="1"/><ctl name="B" value="2"/></path>'
                  '<path name="b"><ctl name="C" value="3"/></path>'
                  '<path name="all"><ctl name="X" value="0"/><path name="a"/>'
                  '<path name="b"/><ctl name="Y" value="9"/></path>')
        self.assertEqual(path(t, "all"),
                         [("X", None, "0"), ("A", None, "1"), ("B", None, "2"),
                          ("C", None, "3"), ("Y", None, "9")])
        self.assertEqual(t.stats["nested_references"], 2)

    def test_control_already_in_path_keeps_its_value(self):
        """audio_route: 'already exists in path - Ignore one in the new sub path'"""
        t = table('<path name="sub"><ctl name="A" value="1"/><ctl name="B" value="2"/></path>'
                  '<path name="p"><ctl name="A" value="7"/><path name="sub"/></path>')
        self.assertEqual(path(t, "p"), [("A", None, "7"), ("B", None, "2")])
        self.assertEqual(t.stats["ignored_in_sub_path"], 1)

    def test_ctl_after_reference_overrides_in_place(self):
        t = table('<path name="sub"><ctl name="A" value="1"/><ctl name="B" value="2"/></path>'
                  '<path name="p"><path name="sub"/><ctl name="A" value="7"/></path>')
        self.assertEqual(path(t, "p"), [("A", None, "7"), ("B", None, "2")])
        self.assertEqual(path(t, "sub"), [("A", None, "1"), ("B", None, "2")])

    def test_reference_to_later_or_unknown_path_is_ignored(self):
        t = table('<path name="p"><path name="later"/><ctl name="A" value="1"/></path>'
                  '<path name="later"><ctl name="B" value="2"/></path>')
        self.assertEqual(path(t, "p"), [("A", None, "1")])
        self.assertTrue(has_warning(t, "unable to find sub path 'later'"))
        self.assertEqual(t.stats["unknown_sub_paths"], 1)

    def test_nested_of_nested(self):
        t = table('<path name="a"><ctl name="A" value="1"/></path>'
                  '<path name="b"><path name="a"/><ctl name="B" value="2"/></path>'
                  '<path name="c"><path name="b"/></path>')
        self.assertEqual(path(t, "c"), [("A", None, "1"), ("B", None, "2")])


class Duplicates(unittest.TestCase):

    def test_first_definition_wins_second_is_dropped(self):
        t = table('<path name="p"><ctl name="A" value="1"/></path>'
                  '<path name="q"><ctl name="Q" value="1"/></path>'
                  '<path name="p"><ctl name="A" value="2"/><ctl name="B" value="3"/>'
                  '<path name="q"/></path>')
        self.assertEqual([p.name for p in t.paths], ["p", "q"])
        self.assertEqual(path(t, "p"), [("A", None, "1")])
        self.assertTrue(has_warning(t, "Path name 'p' already exists"))
        self.assertEqual(t.stats["duplicate_paths"], 1)

    def test_reference_is_not_a_duplicate(self):
        t = table('<path name="p"><ctl name="A" value="1"/></path>'
                  '<path name="q"><path name="p"/></path>')
        self.assertEqual(t.stats["duplicate_paths"], 0)
        self.assertEqual(t.warnings, [])


class Ids(unittest.TestCase):

    def test_all_elements_listed(self):
        t = table('<path name="p"><ctl name="L" id="0" value="0"/>'
                  '<ctl name="L" id="1" value="3"/><ctl name="L" id="2" value="-1"/>'
                  '<ctl name="G" value="130"/></path>')
        self.assertEqual(path(t, "p"), [("L", 0, "0"), ("L", 1, "3"), ("L", 2, "-1"),
                                        ("G", None, "130")])

    def test_elements_grouped_at_first_occurrence_ascending(self):
        t = table('<path name="p"><ctl name="L" id="1" value="3"/><ctl name="G" value="1"/>'
                  '<ctl name="L" id="0" value="4"/></path>')
        self.assertEqual(path(t, "p"), [("L", 0, "4"), ("L", 1, "3"), ("G", None, "1")])

    def test_first_write_also_lands_in_element_0(self):
        """audio_route initialises element 0 of a new path setting with the value"""
        t = table('<path name="p"><ctl name="L" id="2" value="5"/></path>')
        self.assertEqual(path(t, "p"), [("L", 0, "5"), ("L", 2, "5")])

    def test_whole_control_then_id(self):
        t = table('<path name="p"><ctl name="L" value="1"/><ctl name="L" id="1" value="0"/></path>')
        self.assertEqual(path(t, "p"), [("L", None, "1"), ("L", 1, "0")])

    def test_id_then_whole_control(self):
        t = table('<path name="p"><ctl name="L" id="1" value="0"/><ctl name="L" value="1"/></path>')
        self.assertEqual(path(t, "p"), [("L", None, "1")])

    def test_id_is_parsed_like_atoi(self):
        t = table('<path name="p"><ctl name="L" id=" 2x" value="5"/>'
                  '<ctl name="M" id="junk" value="6"/></path>')
        self.assertEqual(path(t, "p"), [("L", 0, "5"), ("L", 2, "5"), ("M", 0, "6")])

    def test_negative_id_is_not_representable(self):
        t = table('<path name="p"><ctl name="L" id="-2" value="5"/><ctl name="A" value="1"/></path>')
        self.assertEqual(path(t, "p"), [("A", None, "1")])
        self.assertEqual(t.stats["skipped_controls"], 1)

    def test_nested_copy_carries_elements(self):
        t = table('<path name="s"><ctl name="L" id="0" value="1"/><ctl name="L" id="1" value="2"/></path>'
                  '<path name="p"><path name="s"/><ctl name="L" id="1" value="9"/></path>')
        self.assertEqual(path(t, "p"), [("L", 0, "1"), ("L", 1, "9")])
        self.assertEqual(path(t, "s"), [("L", 0, "1"), ("L", 1, "2")])


class Syntax(unittest.TestCase):

    def test_comments_are_ignored(self):
        t = table('<!-- c --><path name="p"><!--<ctl name="No" value="1"/>-->'
                  '<ctl name="A" value="1"/><!-- <path name="x"/> --></path>')
        self.assertEqual(path(t, "p"), [("A", None, "1")])
        self.assertEqual(t.warnings, [])

    def test_names_with_spaces_and_special_characters(self):
        t = table('<path name="tone-for handset (a/b)"><ctl name="Gain At High Power (cB)" value="3.50A"/>'
                  '<ctl name="A &amp; B &lt;c&gt; &quot;q&quot;" value="No MIC"/></path>')
        self.assertEqual(path(t, "tone-for handset (a/b)"),
                         [("Gain At High Power (cB)", None, "3.50A"),
                          ('A & B <c> "q"', None, "No MIC")])

    def test_ctl_without_name_or_value_is_skipped(self):
        t = table('<path name="p"><ctl value="1"/><ctl name="A"/><ctl name="B" value="2"/></path>')
        self.assertEqual(path(t, "p"), [("B", None, "2")])
        self.assertEqual(t.stats["skipped_controls"], 2)

    def test_values_hid_cannot_carry_are_skipped(self):
        t = table('<path name="p"><ctl name="A|B" value="1"/><ctl name="C" value="x|y"/>'
                  '<ctl name="D" value="a&#9;b"/><ctl name=" E" value="1"/>'
                  '<ctl name="F" value=""/><ctl name="G" value="2"/></path>'
                  '<path name="q|r"><ctl name="H" value="1"/></path>')
        self.assertEqual(path(t, "p"), [("G", None, "2")])
        self.assertIsNone(t.path("q|r"))
        self.assertEqual(t.stats["skipped_controls"], 5)
        self.assertEqual(t.stats["skipped_paths"], 1)

    def test_bin_attribute_is_not_representable(self):
        t = table('<path name="p"><ctl name="Blob" bin="/vendor/etc/x.bin"/></path>')
        self.assertEqual(path(t, "p"), [])
        self.assertEqual(t.stats["skipped_controls"], 1)

    def test_unnamed_top_level_path_extends_the_previous_one(self):
        """audio_route keeps its current path when a top-level path has no name"""
        t = table('<path name="p"><ctl name="A" value="1"/></path>'
                  '<path><ctl name="B" value="2"/></path>')
        self.assertEqual(path(t, "p"), [("A", None, "1"), ("B", None, "2")])
        self.assertTrue(has_warning(t, "Unnamed path"))

    def test_ctl_below_an_unknown_element_belongs_to_the_current_path(self):
        """only the nesting level tells an initial value from a path setting"""
        t = table('<path name="p"><ctl name="A" value="1"/></path>'
                  '<group><ctl name="B" value="2"/></group>')
        self.assertEqual(path(t, "p"), [("A", None, "1"), ("B", None, "2")])
        self.assertEqual(m.writes(t.defaults), [])

    def test_enum_numeric_fallback_flag(self):
        self.assertFalse(table("").enum_numeric_fallback)
        self.assertTrue(table("", ' enum_mixer_numeric_fallback="true"').enum_numeric_fallback)

    def test_malformed_xml_raises(self):
        with self.assertRaises(ValueError):
            m.parse("<mixer><path name='p'></mixer>")


class Hid(unittest.TestCase):

    def test_text(self):
        t = table('<ctl name="Out Format" value="S32_LE"/>'
                  '<ctl name="Mic List" id="1" value="-1"/>'
                  '<path name="tone-for handset"><ctl name="Tone Biquad0" id="0" value="1065353216"/>'
                  '<ctl name="Gain (cB)" value="130"/></path>'
                  '<path name="empty one"></path>')
        self.assertEqual(m.to_hid(t),
                         "paths\n"
                         "+ defaults\n"
                         "  + ctl Out Format | value: S32_LE\n"
                         "  + ctl Mic List | index: 1 | value: -1\n"
                         "+ path tone-for handset\n"
                         "  + ctl Tone Biquad0 | index: 0 | value: 1065353216\n"
                         "  + ctl Gain (cB) | value: 130\n"
                         "+ path empty one\n"
                         "-\n")

    def test_empty_table(self):
        self.assertEqual(m.to_hid(table("")), "paths\n+ defaults\n-\n")

    def test_name_that_looks_like_an_attribute_gets_an_explicit_tag(self):
        t = table('<path name="mode: x"><ctl name="gain: left" value="1"/>'
                  '<ctl name="Trailing:" value="2"/><ctl name="A:b" value="c: d"/></path>')
        self.assertEqual(m.to_hid(t),
                         "paths\n"
                         "+ defaults\n"
                         "+ path | name: mode: x\n"
                         "  + ctl | name: gain: left | value: 1\n"
                         "  + ctl | name: Trailing: | value: 2\n"
                         "  + ctl A:b | value: c: d\n"
                         "-\n")

    def test_enum_numeric_fallback_attribute(self):
        t = table("", ' enum_mixer_numeric_fallback="true"')
        self.assertEqual(m.to_hid(t), "paths | enum_numeric_fallback: yes\n+ defaults\n-\n")

    def test_deterministic(self):
        xml = read(FIXTURE, "rb")
        self.assertEqual(m.to_hid(m.parse(xml)), m.to_hid(m.parse(xml)))


class Fixture(unittest.TestCase):

    def setUp(self):
        self.t = m.parse(read(FIXTURE, "rb"))

    def test_content(self):
        t = self.t
        self.assertEqual([p.name for p in t.paths],
                         ["tone-for handset", "media-playbackP", "media-playbackP nothing",
                          "handset", "speaker", "speaker-post", "dual-speaker",
                          "handset-mic"])
        self.assertEqual(m.writes(t.defaults)[:2],
                         [("Out Format", None, "S16_LE"), ("Top Amp Switch", None, "0")])
        self.assertEqual(path(t, "handset"),
                         [("Top Amp Current Limit", None, "2.50A"),
                          ("Top Amp Switch", None, "1")])
        self.assertEqual(path(t, "speaker"),
                         [("Top Amp Switch", None, "1"), ("Bottom Amp Switch", None, "1")])
        self.assertEqual(path(t, "dual-speaker"),
                         [("Top Amp Switch", None, "0"), ("Bottom Amp Switch", None, "1"),
                          ("Top Amp Delta File", None, "delta_3.txt")])
        self.assertEqual(path(t, "handset-mic"),
                         [("Mic List", 0, "0"), ("Mic List", 1, "2"), ("Mic List", 2, "-1"),
                          ("Mic Gain At High Power (cB)", None, "130")])
        self.assertEqual(t.stats["paths"], 8)
        self.assertEqual(t.stats["duplicate_paths"], 1)
        self.assertEqual(t.stats["nested_references"], 2)
        self.assertEqual(t.stats["ignored_in_sub_path"], 1)
        self.assertEqual(t.stats["default_controls"], 10)
        self.assertEqual(t.stats["skipped_controls"], 0)

    def _hid_get(self, hid_file, query):
        out = subprocess.run([HID, "get", query, hid_file], check=True,
                             capture_output=True, text=True).stdout
        return out.split("\n")[:-1]

    @unittest.skipUnless(os.access(HID, os.X_OK) and shutil.which("tclsh"),
                         "tool/hid or tclsh not available")
    def test_hid_tool_accepts_and_reads_back_the_output(self):
        """the HID parser of this tree returns every name and value unchanged"""
        xml = ('<mixer>'
               '<ctl name="Gain At High Power (cB)" value="-40"/>'
               '<ctl name="A &amp; B &lt;c&gt; &quot;q&quot; \'p\'" value="No MIC"/>'
               '<ctl name="gain: left" value="a: b"/>'
               '<ctl name="x Foo . bar + baz" value="3.50A"/>'
               '</mixer>')
        for t in (self.t, m.parse(xml)):
            with tempfile.TemporaryDirectory() as d:
                f = os.path.join(d, "paths.hid")
                with open(f, "w") as fd:
                    fd.write(m.to_hid(t))
                subprocess.run([HID, "check", f], check=True, capture_output=True)

                self.assertEqual(self._hid_get(f, "paths | + path | : name"),
                                 [p.name for p in t.paths])

                d_writes = m.writes(t.defaults)
                self.assertEqual(self._hid_get(f, "paths | + defaults | + ctl | : name"),
                                 [w[0] for w in d_writes])
                self.assertEqual(self._hid_get(f, "paths | + defaults | + ctl | : value"),
                                 [w[2] for w in d_writes])
                self.assertEqual(self._hid_get(f, "paths | + defaults | + ctl | : index"),
                                 [str(w[1]) for w in d_writes if w[1] is not None])

                p_writes = [w for p in t.paths for w in m.writes(p.settings)]
                self.assertEqual(self._hid_get(f, "paths | + path | + ctl | : name"),
                                 [w[0] for w in p_writes])
                self.assertEqual(self._hid_get(f, "paths | + path | + ctl | : value"),
                                 [w[2] for w in p_writes])
                self.assertEqual(self._hid_get(f, "paths | + path | + ctl | : index"),
                                 [str(w[1]) for w in p_writes if w[1] is not None])

    def test_command_line(self):
        r = subprocess.run([sys.executable, TOOL, FIXTURE], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        self.assertEqual(r.stdout, m.to_hid(self.t))
        self.assertIn("8 paths", r.stderr)
        self.assertIn("Path name 'handset' already exists", r.stderr)

        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "paths.config")
            r = subprocess.run([sys.executable, TOOL, FIXTURE, "-o", out],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0)
            self.assertEqual(r.stdout, "")
            self.assertEqual(read(out), m.to_hid(self.t))

            bad = os.path.join(d, "bad.xml")
            with open(bad, "w") as fd:
                fd.write("<mixer><path name='p'></mixer>")
            r = subprocess.run([sys.executable, TOOL, bad], capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertEqual(r.stdout, "")


if __name__ == "__main__":
    unittest.main()
