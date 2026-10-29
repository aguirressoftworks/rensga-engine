#!/usr/bin/env python3
"""ctest: codegen is deterministic, strips editor metadata behind one define, and rejects bad input.
Usage: test_reflect_scan.py <repo> <tmpdir>"""
import os
import shutil
import subprocess
import sys

repo, tmp = sys.argv[1], sys.argv[2]
scan = os.path.join(repo, "tools", "reflect", "reflect_scan.py")
shutil.rmtree(tmp, ignore_errors=True)
os.makedirs(tmp)


def run(out, *groups):
    args = [sys.executable, scan, "--out", out]
    for g, paths in groups:
        args += ["--group", g] + paths
    return subprocess.run(args, capture_output=True, text=True)


groups = [("engine", [os.path.join(repo, "engine/core/include/rensga/core/components.h")]),
          ("project", [os.path.join(repo, "templates/psp/Assets/Scripts")]),
          ("tests", [os.path.join(repo, "tests/core"), os.path.join(repo, "tests/game")])]
a, b = os.path.join(tmp, "a"), os.path.join(tmp, "b")
for d in (a, b):
    r = run(d, *groups)
    assert r.returncode == 0, r.stderr
files = []
for root, _, names in os.walk(a):
    for n in names:
        files.append(os.path.relpath(os.path.join(root, n), a))
assert len(files) >= 6, files
for f in sorted(files):
    # manifests embed the output directory; everything else must be byte-identical
    ta = open(os.path.join(a, f)).read().replace(a, "@OUT@")
    tb = open(os.path.join(b, f)).read().replace(b, "@OUT@")
    same = ta == tb
    assert same, "not deterministic: " + f
# second run over the same dir must not touch the files (no mtime churn)
m = os.path.getmtime(os.path.join(a, "engine", "components_h.reflect.gen.cpp"))
assert run(a, *groups).returncode == 0
assert os.path.getmtime(os.path.join(a, "engine", "components_h.reflect.gen.cpp")) == m

gen = open(os.path.join(a, "engine", "components_h.reflect.gen.cpp")).read()
assert "beginType<T>(\"CharacterController\")" in gen and "&T::stepOffset" in gen
pick = open(os.path.join(a, "project", "Pickup_cpp.reflect.gen.cpp")).read()
assert "RENSGA_STRIP_EDITOR_METADATA" in pick and "rangeMax" in pick
assert "Pickup.cpp\"" in pick  # the generated file includes (and so replaces) the source

# editor metadata only ever appears inside the strip guard
for f in files:
    if not f.endswith(".gen.cpp"):
        continue
    depth = 0
    for line in open(os.path.join(a, f)):
        if line.startswith("#ifndef RENSGA_STRIP_EDITOR_METADATA"):
            depth += 1
        elif line.startswith("#endif"):
            depth -= 1
        elif depth == 0 and any(k in line for k in ("->header", "->tooltip", "hasRange", "requireComponent")):
            raise AssertionError("unguarded editor metadata in %s: %s" % (f, line.strip()))
    assert depth == 0

# bad input
bad = os.path.join(tmp, "bad")
os.makedirs(bad)
cases = {
    "dangling.h": "struct X { REFLECT(X)\n RANGE(0,1)\n };\n",
    "badtype.h": "struct Y { REFLECT(Y)\n SERIALIZE_FIELD(std::string, s, \"\")\n };\n",
    "dup.h": "struct Z { REFLECT(Z)\n SERIALIZE_FIELD(i32, a, 0)\n SERIALIZE_FIELD(i32, a, 1)\n };\n",
    "mismatch.h": "struct W { REFLECT(V)\n };\n",
}
for n, text in cases.items():
    p = os.path.join(bad, n)
    open(p, "w").write(text)
    r = run(os.path.join(tmp, "o_" + n), ("g", [p]))
    assert r.returncode != 0 and "reflect_scan" in r.stderr, n
# comments and nested namespaces are handled
ok = os.path.join(tmp, "ok.h")
open(ok, "w").write("namespace a { namespace b {\n// REFLECT(Nope)\nstruct Q : Base { REFLECT(Q)\n /* RANGE(1,2) */\n"
                    " SERIALIZE_FIELD(Vector3, v, Vector3(1, 2, 3))\n}; } }\n")
assert run(os.path.join(tmp, "o_ok"), ("g", [ok])).returncode == 0
assert "using T = ::a::b::Q;" in open(os.path.join(tmp, "o_ok", "g", "ok_h.reflect.gen.cpp")).read()
print("reflect_scan: ok")
