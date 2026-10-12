#!/usr/bin/env python3
import unittest
from inventory_mkw_gx_callers import family_hint, inventory, registrations


class InventoryTests(unittest.TestCase):
    def test_masks_comments_strings_and_preserves_line_numbers(self):
        source = '''// PPC_NATIVE_OVERRIDE_VOID(80160000, False, (), ());
const char* s = "REGISTER_NATIVE_FUNCTION(0x80160001, Fake)";
const char* r = R"tag(PPC_NATIVE_OVERRIDE_VOID(80160002, Fake, (), ()))tag";
/* REGISTER_NATIVE_FUNCTION(0x80160003, Fake); */
PPC_NATIVE_OVERRIDE_VOID(80160004, GXBegin, (), ());
REGISTER_NATIVE_FUNCTION_AS(0x80160005u, DrawQuad, "DrawQuad");
PPC_NATIVE_OVERRIDE(80160006, GXResult, int, (), ());
'''
        result = registrations(source, 'gx.cpp')
        self.assertEqual([e['address'] for e in result], [0x80160004, 0x80160005, 0x80160006])
        self.assertEqual([e['line'] for e in result], [5, 6, 7])

    def metadata(self, dependencies):
        return dict(format='mkw-base-translation-output', formatVersion=2,
                    quality=dict(unsupportedInstructionCount=0), functions=[dict(
                        entryPoint=0x80006000, relativePath='func.cpp', build=dict(
                            symbol='func_80006000', directCallDependencies=dependencies))])

    def test_unique_edges_and_unreferenced_targets(self):
        result = inventory(self.metadata([0x80160004, 0x80160004, 0x81234568]),
                           registrations('PPC_NATIVE_OVERRIDE_VOID(80160004, GXBegin, (), ());\n'
                                         'PPC_NATIVE_OVERRIDE_VOID(80160005, GXEnd, (), ());', 'gx.cpp'),
                           {0x80006000: 'nw4r::lyt::DrawQuad'})
        self.assertEqual(result['summary']['static_edges'], 1)
        self.assertEqual(result['summary']['translated_gx_callers'], 1)
        self.assertEqual(result['summary']['gx_declared_targets'], 2)
        self.assertEqual(result['callers'][0]['family_hint'], 'ui_text')
        self.assertEqual(result['targets'][1]['translated_callers'], 0)

    def test_rejects_unknown_schema_and_missing_dependencies(self):
        with self.assertRaises(ValueError):
            inventory(dict(format='unknown'), [])
        bad = self.metadata([])
        del bad['functions'][0]['build']['directCallDependencies']
        with self.assertRaises(ValueError):
            inventory(bad, [])
        with self.assertRaises(ValueError):
            inventory(self.metadata(['0x80160004']), [])

    def test_duplicate_function_is_rejected(self):
        bad = self.metadata([])
        bad['functions'] *= 2
        with self.assertRaises(ValueError):
            inventory(bad, [])

    def test_effect_quad_does_not_become_a_ui_family(self):
        self.assertEqual(family_hint('nw4r::ef::DrawQuad'), 'effects')
        self.assertEqual(family_hint('GX::Begin'), 'gx_sdk')


if __name__ == '__main__':
    unittest.main()
