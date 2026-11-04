"""Catalog validation and proof that a new JSON entry + C file is sufficient."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from generate_apps import generate


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(dir=ROOT/'build')
        self.root=Path(self.directory.name).resolve()
        assert self.root.is_relative_to((ROOT/'build').resolve())
        self.manifest=self.root/'apps.json'
        (self.root/'probe.c').write_text('')
        self.entry={'id':'probe','label':'PROBE','source':'probe.c'}

    def tearDown(self):
        self.directory.cleanup()

    def emit(self,entries):
        self.manifest.write_text(json.dumps(entries))
        generate(self.manifest,self.root/'generated')

    def test_validation(self):
        for entries in [[],{},[self.entry,self.entry],
                        [dict(self.entry,id='Bad ID')],
                        [dict(self.entry,label='ELEVEN CHARS')],
                        [dict(self.entry,label='BAD?')],
                        [dict(self.entry,source='missing.c')],
                        [dict(self.entry,source='../outside.c')],
                        [dict(self.entry,source='probe.c;$bad')],
                        [dict(self.entry,extra=True)]]:
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                self.emit(entries)

    def test_real_catalog(self):
        generate(ROOT/'apps.json',self.root/'generated')
        catalog=json.loads((ROOT/'apps.json').read_text())
        output=(self.root/'generated/app_registry.c').read_text()
        positions=[output.index(json.dumps(entry['id'])) for entry in catalog]
        self.assertEqual(positions,sorted(positions))
        self.assertEqual(output.count('extern const App '),len(catalog))

    def test_add_and_reorder_without_central_edits(self):
        # These are the only two authored files: the JSON entry and its app.
        (self.root/'probe.c').write_text('''#include "app.h"
extern unsigned entered,drawn,left;
static void enter(void) { ++entered; }
static void leave(void) { ++left; }
static void draw(void) { ++drawn; }
static void button(unsigned b) { if(b==1) app_return_home(); }
const App probe_app={.enter=enter,.leave=leave,.draw=draw,.button=button};
''')
        self.emit([self.entry])
        harness=self.root/'test.c'
        harness.write_text('''#include <assert.h>
#include <string.h>
#include "app.h"
#include "home.h"
HomeMenu home_menu;
unsigned entered,drawn,left;
bool app_host_can_open(void) { return true; }
void app_host_prepare(void) {}
void app_host_home(void) {}
int main(void) {
    assert(app_entry_count==1 && !strcmp(app_entries[0].id,"probe"));
    assert(app_open(0) && entered==1);
    app_draw(); assert(drawn==1);
    app_button(1); assert(!app_current() && left==1 && home_menu.selected==0);
    assert(!app_open(1)); return 0;
}
''')
        compiler=os.environ.get('CC') or shutil.which('gcc')
        if not compiler:
            self.fail('A native C compiler is required (CC or gcc on PATH)')
        executable=self.root/('probe-test.exe' if os.name=='nt' else 'probe-test')
        subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),
                        str(harness),str(self.root/'probe.c'),str(ROOT/'src/app.c'),
                        str(ROOT/'src/home_menu.c'),str(self.root/'generated/app_registry.c'),
                        '-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)
        # Reordering is controlled solely by manifest order.
        (self.root/'second.c').write_text('')
        second={'id':'second','label':'SECOND','source':'second.c'}
        self.emit([second,self.entry])
        output=(self.root/'generated/app_registry.c').read_text()
        self.assertLess(output.index('"second"'),output.index('"probe"'))


if __name__=='__main__':
    unittest.main()
