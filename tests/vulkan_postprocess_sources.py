#!/usr/bin/env python3
"""Extract actual C shader strings for independent SPIR-V compilation checks."""
import ast
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk.c").read_text()
header = (root / "xboxrecomp/src/nv2a_vk/nv2a_vk_prerotation.h").read_text()
mapping = header.split("#define PRESENT_LOGICAL_COORD_GLSL", 1)[1]
strings = lambda text: "".join(ast.literal_eval(item) for item in re.findall(r'"(?:\\.|[^"\\])*"', text))
assert len(sys.argv) == 3, "expected vertex and fragment output paths"
for name, output in zip(("s_pp_vs", "s_pp_fs"), sys.argv[1:]):
    body = source.split(f"static const char {name}[] =", 1)[1].split('";\n', 1)[0] + '"'
    body = body.replace("PRESENT_LOGICAL_COORD_GLSL", mapping)
    Path(output).write_text(strings(body))
