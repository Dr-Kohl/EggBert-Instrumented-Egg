"""Validate apps.json and emit the compiled registry and CMake source list.

The firmware does not parse JSON or need a filesystem. Add a manifest entry
and its C file defining `const App <id>_app`; CMake discovers both automatically.
"""
import argparse
import json
import re
from pathlib import Path


def generate(manifest, output):
    manifest, output = Path(manifest).resolve(), Path(output)
    root = manifest.parent
    entries = json.loads(manifest.read_text(encoding='utf-8'))
    if not isinstance(entries, list) or not entries:
        raise ValueError('App catalog must be a nonempty JSON array')
    ids, sources, declarations, rows = set(), [], [], []
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {'id', 'label', 'source'}:
            raise ValueError('Each app requires exactly id, label, and source')
        ident, label, source = entry['id'], entry['label'], entry['source']
        if not isinstance(ident, str) or not re.fullmatch(r'[a-z][a-z0-9_]*', ident) or ident in ids:
            raise ValueError(f'Invalid or duplicate app id: {ident!r}')
        if not isinstance(label, str) or not re.fullmatch(r'[A-Z0-9 .+/\-]{1,10}', label) or not label.strip():
            raise ValueError(f'App {ident}: label must fit 10 supported OLED characters')
        if not isinstance(source, str) or '\\' in source or any(c in source for c in ';"\n\r$'):
            raise ValueError(f'App {ident}: invalid source path')
        path = (root / source).resolve()
        if not path.is_relative_to(root) or path.suffix != '.c' or not path.is_file():
            raise ValueError(f'App {ident}: source must be an existing .c file inside the project')
        if path in sources:
            raise ValueError(f'App {ident}: each app must own a separate source file')
        ids.add(ident)
        sources.append(path)
        declarations.append(f'extern const App {ident}_app;')
        rows.append(f'    {{{json.dumps(ident)}, {json.dumps(label)}, &{ident}_app}},')
    generated = ('// Generated from apps.json; do not edit.\n#include "app.h"\n' +
                 '\n'.join(declarations) + '\nconst AppEntry app_entries[] = {\n' +
                 '\n'.join(rows) + '\n};\n' +
                 'const size_t app_entry_count = sizeof app_entries / sizeof app_entries[0];\n')
    cmake = 'set(EGGBERT_APP_SOURCES\n' + ''.join(f'    "{p.as_posix()}"\n' for p in sources) + ')\n'
    output.mkdir(parents=True, exist_ok=True)
    for name, text in [('app_registry.c', generated), ('app_sources.cmake', cmake)]:
        path = output / name
        if not path.exists() or path.read_text(encoding='utf-8') != text:
            path.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.manifest, args.output_dir)
    except (ValueError, OSError) as error:
        parser.exit(1, f'App catalog error: {error}\n')
