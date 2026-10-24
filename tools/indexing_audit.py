# Audit d'indexation : volumes par dossier, configuration C++ generee,
# couverture de compile_commands.json et exclusions du workspace.
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
def load_jsonc(path):
    text = path.read_text(encoding='utf-8-sig')
    text = re.sub(r'"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\r\n]*',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    return json.loads(text)

sizes = Counter()
counts = Counter()
for file in root.rglob('*'):
    if file.is_file():
        group = file.relative_to(root).parts[0]
        sizes[group] += file.stat().st_size
        counts[group] += 1

props = load_jsonc(root / '.vscode/c_cpp_properties.json')['configurations'][0]
includes = props.get('includePath', [])
include_report = {
    'count': len(includes),
    'empty_entries': sum(not entry for entry in includes),
    'duplicates': [entry for entry, n in Counter(includes).items() if n > 1],
    'missing_paths': [entry for entry in includes if entry and not Path(entry).exists()],
    'compiler_exists': Path(props['compilerPath']).is_file(),
    'browse_limit_included_headers': props.get('browse', {}).get('limitSymbolsToIncludedHeaders'),
    'compile_commands_setting': props.get('compileCommands'),
}

records = load_jsonc(root / 'compile_commands.json')
files = [Path(record['file']).resolve() for record in records]
local_sources = sorted(p.resolve() for p in (root / 'src').glob('*.cpp'))
covered = [p for p in local_sources if p in files]
missing_sources = [str(p.relative_to(root)) for p in local_sources if p not in files]
missing_inputs = [str(p) for p in files if not p.is_file()]
missing_directories = sorted({record['directory'] for record in records if not Path(record['directory']).is_dir()})
platformio_defines = sorted(set(re.findall(r'-DPLATFORMIO=([^\s]+)', '\n'.join(r.get('command', '') for r in records))))

print(json.dumps({
    'files_by_top_level': {name: {'files': counts[name], 'bytes': sizes[name]} for name in sorted(sizes)},
    'cpp_configuration': include_report,
    'compilation_database': {
        'modified': datetime.fromtimestamp((root / 'compile_commands.json').stat().st_mtime).astimezone().isoformat(),
        'entries': len(records), 'duplicate_source_entries': len(files) - len(set(files)),
        'local_cpp_sources': len(local_sources), 'local_cpp_covered': len(covered),
        'missing_local_sources': missing_sources,
        'missing_input_files_count': len(missing_inputs), 'missing_input_examples': missing_inputs[:8],
        'missing_working_directories': missing_directories,
        'platformio_defines': platformio_defines,
    },
    'workspace_settings': load_jsonc(root / 'Projet ventillateur.code-workspace').get('settings', {}),
}, ensure_ascii=False, indent=2))
