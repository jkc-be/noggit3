#!/usr/bin/env python3
"""Read an AzerothCore mysqldump locally. Never connects to a server."""
import json
import re
import sys
from pathlib import Path


def split_row(line):
    fields, field, quoted, escape = [], '', False, False
    for c in line[1:line.rfind(')')]:
        if escape:
            field += {'n':'\n','r':'\r','t':'\t','0':'\0'}.get(c,c)
            escape = False
        elif c == '\\' and quoted:
            escape = True
        elif c == "'":
            quoted = not quoted
        elif c == ',' and not quoted:
            fields.append(field)
            field = ''
        else:
            field += c
    fields.append(field)
    return fields


def tuples(text):
    """Accept both the checked-in dump and ordinary mysqldump INSERT statements."""
    for match in re.finditer(r"INSERT INTO `gameobject_template` VALUES\s*", text):
        quoted = escape = False
        start = None
        for i in range(match.end(), len(text)):
            c = text[i]
            if escape:
                escape = False
            elif quoted and c == "\\":
                escape = True
            elif c == "'":
                quoted = not quoted
            elif not quoted:
                if c == ';':
                    break
                if c == '(':
                    start = i
                elif c == ')' and start is not None:
                    yield text[start:i+1]
                    start = None


def main():
    source, out = map(Path,sys.argv[1:])
    templates = []
    for line in tuples(source.read_text()):
        f = split_row(line)
        if len(f) != 35:
            raise ValueError('Expected AzerothCore gameobject_template column order (35 fields)')
        templates.append(dict(entry=int(f[0]),type=int(f[1]),display=int(f[2]),name=f[3],size=float(f[7])))
    if not templates:
        raise ValueError('No template rows found')
    out.write_text(json.dumps({'source':str(source),'templates':templates},ensure_ascii=False,indent=2))
    print(f'Catalog contains {len(templates)} templates')

if __name__ == '__main__': main()
