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
          ("project", [os.path.join(repo, "templates/psp-3d/Assets/Scripts")]),
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
        elif depth == 0 and any(k in line for k in ("->header", "->tooltip", "hasRange", "requireComponent", "hideInInspector", "->space", "executeInEditMode", "addContextMenu", "enumLabels")):
            raise AssertionError("unguarded editor metadata in %s: %s" % (f, line.strip()))
    assert depth == 0

# bad input
bad = os.path.join(tmp, "bad")
os.makedirs(bad)
cases = {
    "dangling.h": "struct X { REFLECT(X)\n RANGE(0,1)\n };\n",
    "badtype.h": "struct Y { REFLECT(Y)\n SERIALIZE_FIELD(std::string, s, \"\")\n };\n",
    "dup.h": "struct Z { REFLECT(Z)\n SERIALIZE_FIELD(i32, a, 0)\n SERIALIZE_FIELD(i32, a, 1)\n };\n",
    "dupmethod.h": "struct M1 { REFLECT(M1)\n METHOD(Go)\n METHOD(Go)\n };\n",
    "badmethod.h": "struct M2 { REFLECT(M2)\n METHOD(a.b)\n };\n",
    "badhide.h": "struct H1 { REFLECT(H1)\n HIDE_IN_INSPECTOR(1)\n SERIALIZE_FIELD(i32, a, 0)\n };\n",
    "badspace.h": "struct H2 { REFLECT(H2)\n SPACE(abc)\n SERIALIZE_FIELD(i32, a, 0)\n };\n",
    "badmenu.h": "struct H3 { REFLECT(H3)\n CONTEXT_MENU(\"Go\")\n int x;\n };\n",
    "dangling_hide.h": "struct H4 { REFLECT(H4)\n HIDE_IN_INSPECTOR()\n };\n",
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
# F3.05: METHOD(Name) and UnityEvent fields
me = os.path.join(tmp, "me.h")
open(me, "w").write("struct Door { REFLECT(Door)\n SERIALIZE_FIELD(UnityEvent, OnOpened, {})\n METHOD(Open)\n METHOD(SetSpeed)\n"
                    " void Open(); void SetSpeed(Scalar s);\n // METHOD(Hidden)\n};\n")
assert run(os.path.join(tmp, "o_me"), ("g", [me])).returncode == 0
g = open(os.path.join(tmp, "o_me", "g", "me_h.reflect.gen.cpp")).read()
assert "FieldType::UnityEvent" in g and "addMethod<T, &T::Open>(ti, \"Open\")" in g and "&T::SetSpeed" in g
assert "Hidden" not in g and g.index("addField") < g.index("addMethod")
# F3.07: editor attributes
at = os.path.join(tmp, "at.h")
open(at, "w").write("struct Gadget : Behaviour { REFLECT(Gadget)\n EXECUTE_IN_EDIT_MODE()\n REQUIRE_COMPONENT(Camera)\n"
                    " SPACE()\n HIDE_IN_INSPECTOR()\n SERIALIZE_FIELD(i32, secret, 1)\n SPACE(20)\n TOOLTIP(\"t\")\n SERIALIZE_FIELD(Scalar, a, 1)\n"
                    " CONTEXT_MENU(\"Fill It\")\n void Fill();\n METHOD(Fill)\n void OnValidate() override;\n};\n")
assert run(os.path.join(tmp, "o_at"), ("g", [at])).returncode == 0
g = open(os.path.join(tmp, "o_at", "g", "at_h.reflect.gen.cpp")).read()
assert "f->hideInInspector = true;" in g and "f->space = 8;" in g and "f->space = 20;" in g
assert "ti->executeInEditMode = true;" in g and 'addContextMenu(ti, "Fill It", "Fill")' in g
assert g.count('addMethod<T, &T::Fill>') == 1, "CONTEXT_MENU method registered once"
# F6: String, Enum, Vector2, arrays, APPENDED_FIELDS
f6 = os.path.join(tmp, "f6.h")
open(f6, "w").write("struct Thing : Component { REFLECT(Thing)\n SERIALIZE_FIELD(Scalar, a, 1)\n SERIALIZE_FIELD(String32, label, String32(\"x\"))\n"
                    " TOOLTIP(\"mode\")\n SERIALIZE_ENUM(mode, 1, \"Horizontal|Vertical|Grid\")\n SERIALIZE_FIELD(Vector2, size, Vector2(1, 1))\n"
                    " APPENDED_FIELDS()\n SERIALIZE_ARRAY(Vector3, points, 16)\n SERIALIZE_ARRAY(Guid, refs, 4)\n SERIALIZE_ARRAY(i32, ids, 8)\n};\n")
assert run(os.path.join(tmp, "o_f6"), ("g", [f6])).returncode == 0
g = open(os.path.join(tmp, "o_f6", "g", "f6_h.reflect.gen.cpp")).read()
assert "FieldType::String, &T::label" in g and "FieldType::Enum, &T::mode" in g and "FieldType::Vector2, &T::size" in g
assert 'f->enumLabels = "Horizontal|Vertical|Grid";' in g and 'f->tooltip = "mode";' in g
assert 'addArrayField(ti, "points", &T::points)' in g and 'addArrayField(ti, "refs", &T::refs)' in g and "ti->trimFloor = 4;" in g
f6bad = {
    "e1.h": "struct E1 { REFLECT(E1)\n SERIALIZE_ENUM(m, 0)\n };\n",
    "e2.h": "struct E2 { REFLECT(E2)\n SERIALIZE_ENUM(m, 0, \"A||B\")\n };\n",
    "a1.h": "struct A1 { REFLECT(A1)\n SERIALIZE_ARRAY(Color32, c, 4)\n };\n",
    "a2.h": "struct A2 { REFLECT(A2)\n SERIALIZE_ARRAY(i32, c, 0)\n };\n",
    "a3.h": "struct A3 { REFLECT(A3)\n SERIALIZE_ARRAY(i32, c, 65)\n };\n",
    "ap.h": "struct AP { REFLECT(AP)\n APPENDED_FIELDS(1)\n };\n",
}
for n, text in f6bad.items():
    p = os.path.join(bad, n)
    open(p, "w").write(text)
    r = run(os.path.join(tmp, "o_" + n), ("g", [p]))
    assert r.returncode != 0 and "reflect_scan" in r.stderr, n
print("reflect_scan: ok")
