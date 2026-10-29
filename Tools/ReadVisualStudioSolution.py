"""Read the generated slnx or legacy sln solution project mappings."""

import re
import xml.etree.ElementTree as ET

from WorkspaceCommon import full_path


def read_projects(build_root):
    build_root = full_path(build_root)
    slnx = build_root / 'UrsaEngine.slnx'
    projects = {}
    if slnx.is_file():
        root = ET.parse(slnx).getroot()
        for parent in root.iter():
            for node in parent.findall('Project[@Path]'):
                path = full_path(build_root / node.attrib['Path'])
                projects[path.stem] = dict(path=path, folder=parent.get('Name', '').strip('/') if parent.tag == 'Folder' else '', node=node)
        return projects
    sln = build_root / 'UrsaEngine.sln'
    if not sln.is_file():
        raise RuntimeError(f'No generated UrsaEngine solution at {build_root}')
    text = sln.read_text(encoding='utf-8-sig')
    entries = {guid.upper(): (kind.upper(), name, path) for kind, name, path, guid in
               re.findall(r'Project\("\{([^}]+)\}"\) = "([^"]+)", "([^"]+)", "\{([^}]+)\}"', text)}
    nested = re.search(r'GlobalSection\(NestedProjects\).*?EndGlobalSection', text, re.S)
    parents = {child.upper(): parent.upper() for child, parent in
               re.findall(r'\{([^}]+)\} = \{([^}]+)\}', nested[0] if nested else '')}
    for guid, (kind, name, path) in entries.items():
        if kind == '2150E333-8FDC-42A3-9474-1A3956D46DE8':
            continue
        parts, seen = [], set()
        while guid in parents:
            if guid in seen:
                raise RuntimeError('Cyclic solution folder mapping')
            seen.add(guid)
            guid = parents[guid]
            parts.insert(0, entries[guid][1])
        projects[name] = dict(path=full_path(build_root / path), folder='/'.join(parts), node=None)
    return projects
