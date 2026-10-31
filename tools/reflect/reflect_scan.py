#!/usr/bin/env python3
"""Rensga reflection scanner (F1.05). Reads headers/sources that use REFLECT / SERIALIZE_FIELD / RANGE / HEADER /
TOOLTIP / REQUIRE_COMPONENT and writes one <stem>.reflect.gen.cpp per file with the per-target registration table,
plus <group>.cmake listing the sources to compile.

  reflect_scan.py --out DIR --group NAME PATH... [--group NAME2 PATH...]

PATH is a file or a directory (scanned for *.h, *.hpp, *.cpp, sorted). For a .cpp that contains reflected types the
generated file #includes the original and REPLACES it in the source list; for a header it is an extra source.
Output is deterministic (no timestamps; files written only when changed). Plain Python 3 stdlib only.
Not available inside the cross toolchain images, so it runs on the host before a cross build (tools/reflect/prescan.sh).
"""
import os
import re
import sys

FIELD_TYPES = {
    "Scalar": "Scalar", "float": "Scalar", "i32": "I32", "int32_t": "I32", "int": "I32", "bool": "Bool",
    "Vector3": "Vector3", "Quaternion": "Quaternion", "Color32": "Color32", "Guid": "Guid",
    "UnityEvent": "UnityEvent", "String32": "String", "Vector2": "Vector2",
}
# F6: element types of SERIALIZE_ARRAY (reflect.h FixedArray<T, N>)
ARRAY_ELEMS = {"Scalar": "Scalar", "float": "Scalar", "i32": "I32", "int32_t": "I32", "int": "I32", "Guid": "Guid",
               "Vector3": "Vector3"}


class ScanError(Exception):
    pass


def mask(text):
    """Blank out comments and preprocessor directives (keeping newlines and string literals)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == "#" and re.match(r"[ \t]*$", text[text.rfind("\n", 0, i) + 1:i]):
            j = i
            while j < n and text[j] != "\n":
                j += 2 if text[j] == "\\" and j + 1 < n else 1
            out.append(re.sub(r"[^\n]", " ", text[i:min(j, n)]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def call_args(text, pos):
    """text[pos] == '(': returns (top-level comma separated args, index after ')')."""
    depth, args, cur, i = 0, [], [], pos
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "([{":
            depth += 1
            if depth > 1:
                cur.append(c)
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i + 1
            cur.append(c)
        elif c == "," and depth == 1:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    raise ScanError("unbalanced parentheses")


TOKEN = re.compile(
    r"(?P<ns>\bnamespace\s*(?P<nsname>[A-Za-z_][\w:]*)?\s*\{)"
    r"|(?P<cls>(?<!enum\s)\b(?:struct|class)\s+(?P<clsname>[A-Za-z_]\w*)\b[^;{}()]*\{)"
    r"|(?P<mac>\b(?P<macname>REFLECT|SERIALIZE_FIELD|SERIALIZE_ENUM|SERIALIZE_ARRAY|APPENDED_FIELDS|RANGE|HEADER|TOOLTIP|REQUIRE_COMPONENT|METHOD|HIDE_IN_INSPECTOR|SPACE|EXECUTE_IN_EDIT_MODE|CONTEXT_MENU)\s*\()"
    r"|(?P<open>\{)|(?P<close>\})"
)


NO_PENDING = {"header": None, "tooltip": None, "range": None, "hide": False, "space": None}
METHOD_DECL = re.compile(r"\s*;?\s*(?:virtual\s+)?void\s+([A-Za-z_]\w*)\s*\(\s*\)")


def scan_text(raw, fname):
    text = mask(raw)
    types, stack, pending = [], [], None
    pos = 0
    pending = dict(NO_PENDING)

    def line(p):
        return text.count("\n", 0, p) + 1

    def where(p):
        return "%s:%d" % (fname, line(p))

    def cur_type(p, what):
        if not types or not stack or stack[-1][0] != "cls" or types[-1]["stackdepth"] != len(stack):
            raise ScanError("%s: %s outside a REFLECT class body" % (where(p), what))
        return types[-1]

    while True:
        m = TOKEN.search(text, pos)
        if not m:
            break
        pos = m.end()
        if m.group("ns"):
            stack.append(("ns", m.group("nsname") or ""))
        elif m.group("cls"):
            stack.append(("cls", m.group("clsname")))
        elif m.group("open"):
            stack.append(("blk", ""))
        elif m.group("close"):
            if not stack:
                raise ScanError("%s: unbalanced '}'" % where(m.start()))
            kind, _ = stack.pop()
            if kind == "cls" and types and types[-1]["stackdepth"] == len(stack) + 1:
                if any(pending.values()):
                    raise ScanError("%s: HEADER/TOOLTIP/RANGE without a following SERIALIZE_FIELD" % where(m.start()))
        else:
            name = m.group("macname")
            args, pos = call_args(text, m.end() - 1)
            if name == "REFLECT":
                if len(args) != 1 or not stack or stack[-1][0] != "cls" or stack[-1][1] != args[0]:
                    raise ScanError("%s: REFLECT(%s) must be the first line in the class body of that class"
                                    % (where(m.start()), ",".join(args)))
                q = [n for k, n in stack if k in ("ns", "cls") and n]
                if any(k == "blk" for k, _ in stack):
                    raise ScanError("%s: reflected types cannot be declared inside a function" % where(m.start()))
                types.append({"name": args[0], "qual": "::" + "::".join(q), "fields": [], "requires": [], "methods": [], "menus": [], "edit_mode": False, "trim": None,
                              "stackdepth": len(stack)})
            elif name == "SERIALIZE_FIELD":
                t = cur_type(m.start(), name)
                if len(args) != 3:
                    raise ScanError("%s: SERIALIZE_FIELD(type, name, default) needs 3 arguments" % where(m.start()))
                base = args[0].split("::")[-1].strip()
                if base not in FIELD_TYPES:
                    raise ScanError("%s: unsupported field type '%s'" % (where(m.start()), args[0]))
                if any(f["name"] == args[1] for f in t["fields"]):
                    raise ScanError("%s: duplicate field '%s'" % (where(m.start()), args[1]))
                t["fields"].append({"name": args[1], "type": FIELD_TYPES[base], "ctype": args[0], **pending})
                pending = dict(NO_PENDING)
            elif name == "SERIALIZE_ENUM":
                t = cur_type(m.start(), name)
                if len(args) != 3 or not args[2].startswith('"') or not re.match(r"^[A-Za-z_]\w*$", args[0]):
                    raise ScanError("%s: SERIALIZE_ENUM(name, default, \"A|B|C\") needs a field name, a default and a string literal" % where(m.start()))
                if any(f["name"] == args[0] for f in t["fields"]):
                    raise ScanError("%s: duplicate field '%s'" % (where(m.start()), args[0]))
                if "||" in args[2] or args[2].startswith('"|') or args[2].endswith('|"') or args[2] == '""':
                    raise ScanError("%s: SERIALIZE_ENUM labels must be non-empty, separated by single '|'" % where(m.start()))
                t["fields"].append({"name": args[0], "type": "Enum", "ctype": "i32", "labels": args[2], **pending})
                pending = dict(NO_PENDING)
            elif name == "SERIALIZE_ARRAY":
                t = cur_type(m.start(), name)
                if len(args) != 3 or not re.match(r"^[A-Za-z_]\w*$", args[1]) or not re.match(r"^\d+$", args[2]) or not (1 <= int(args[2]) <= 64):
                    raise ScanError("%s: SERIALIZE_ARRAY(type, name, capacity 1..64) needs a type, a field name and a number" % where(m.start()))
                base = args[0].split("::")[-1].strip()
                if base not in ARRAY_ELEMS:
                    raise ScanError("%s: unsupported array element type '%s' (i32, Scalar, Guid, Vector3)" % (where(m.start()), args[0]))
                if any(f["name"] == args[1] for f in t["fields"]):
                    raise ScanError("%s: duplicate field '%s'" % (where(m.start()), args[1]))
                t["fields"].append({"name": args[1], "type": "Array", "ctype": "FixedArray<%s,%s>" % (args[0], args[2]),
                                    "elem": ARRAY_ELEMS[base], "cap": int(args[2]), **pending})
                pending = dict(NO_PENDING)
            elif name == "APPENDED_FIELDS":
                t = cur_type(m.start(), name)
                if args != [""]:
                    raise ScanError("%s: APPENDED_FIELDS() takes no arguments" % where(m.start()))
                if t["trim"] is None:
                    t["trim"] = len(t["fields"])
            elif name == "HIDE_IN_INSPECTOR":
                cur_type(m.start(), name)
                if args != [""]:
                    raise ScanError("%s: HIDE_IN_INSPECTOR() takes no arguments" % where(m.start()))
                pending["hide"] = True
            elif name == "SPACE":
                cur_type(m.start(), name)
                if len(args) > 1 or (args[0] and not re.match(r"^\d{1,3}$", args[0])) or (args[0] and int(args[0]) > 255):
                    raise ScanError("%s: SPACE() or SPACE(pixels 0..255)" % where(m.start()))
                pending["space"] = int(args[0]) if args[0] else 8
            elif name == "EXECUTE_IN_EDIT_MODE":
                t = cur_type(m.start(), name)
                if args != [""]:
                    raise ScanError("%s: EXECUTE_IN_EDIT_MODE() takes no arguments" % where(m.start()))
                t["edit_mode"] = True
            elif name == "CONTEXT_MENU":
                t = cur_type(m.start(), name)
                if len(args) != 1 or not args[0].startswith('"'):
                    raise ScanError("%s: CONTEXT_MENU needs a string literal label" % where(m.start()))
                md = METHOD_DECL.match(text, pos)
                if not md:
                    raise ScanError("%s: CONTEXT_MENU(\"Label\") must be followed by a void() method declaration" % where(m.start()))
                t["menus"].append((args[0], md.group(1)))
            elif name == "RANGE":
                cur_type(m.start(), name)
                if len(args) != 2:
                    raise ScanError("%s: RANGE(lo, hi)" % where(m.start()))
                pending["range"] = (args[0], args[1])
            elif name in ("HEADER", "TOOLTIP"):
                cur_type(m.start(), name)
                if len(args) != 1 or not args[0].startswith('"'):
                    raise ScanError("%s: %s needs a string literal" % (where(m.start()), name))
                pending[name.lower()] = args[0]
            elif name == "METHOD":
                t = cur_type(m.start(), name)
                if len(args) != 1 or not re.match(r"^[A-Za-z_]\w*$", args[0]):
                    raise ScanError("%s: METHOD(Name) takes one method name" % where(m.start()))
                if args[0] in t["methods"]:
                    raise ScanError("%s: duplicate METHOD '%s' (overloads are not supported)" % (where(m.start()), args[0]))
                t["methods"].append(args[0])
            elif name == "REQUIRE_COMPONENT":
                t = cur_type(m.start(), name)
                if len(args) != 1 or not re.match(r"^[A-Za-z_][\w:]*$", args[0]):
                    raise ScanError("%s: REQUIRE_COMPONENT(Type)" % where(m.start()))
                t["requires"].append(args[0].split("::")[-1])
    if stack:
        raise ScanError("%s: unbalanced '{'" % fname)
    return types


def ident(path):
    return re.sub(r"\W", "_", os.path.basename(path))


def emit(path, types):
    ap = os.path.abspath(path)
    fn = "rensga_reflect_register_" + ident(path)
    o = ["// GENERATED by tools/reflect/reflect_scan.py from %s. Do not edit." % os.path.basename(path),
         '#include "%s"' % ap.replace("\\", "/"),
         '#include "rensga/core/reflect.h"', '#include "rensga/core/reflect_method.h"',
         '#include "rensga/core/unity_event.h"', "",
         "void %s() {" % fn,
         "  static bool done = false;",
         "  if (done) return;",
         "  done = true;"]
    for t in types:
        o += ["  {", "    using T = %s;" % t["qual"],
              '    ::rensga::TypeInfo* ti = ::rensga::reflect::beginType<T>("%s");' % t["name"],
              "    if (ti) {", "      ::rensga::FieldDesc* f = nullptr;", "      (void)f;"]
        for fd in t["fields"]:
            if fd["type"] == "Array":
                o.append('      f = ::rensga::reflect::addArrayField(ti, "%s", &T::%s);' % (fd["name"], fd["name"]))
            else:
                o.append('      f = ::rensga::reflect::addField(ti, "%s", ::rensga::FieldType::%s, &T::%s);'
                         % (fd["name"], fd["type"], fd["name"]))
            meta = []
            if fd.get("labels"):
                meta.append("      f->enumLabels = %s;" % fd["labels"])
            if fd["header"]:
                meta.append("      f->header = %s;" % fd["header"])
            if fd["tooltip"]:
                meta.append("      f->tooltip = %s;" % fd["tooltip"])
            if fd["hide"]:
                meta.append("      f->hideInInspector = true;")
            if fd["space"] is not None:
                meta.append("      f->space = %d;" % fd["space"])
            if fd["range"]:
                meta.append("      f->hasRange = true;")
                meta.append("      f->rangeMin = static_cast<::rensga::Scalar>(%s);" % fd["range"][0])
                meta.append("      f->rangeMax = static_cast<::rensga::Scalar>(%s);" % fd["range"][1])
            if meta:
                o += ["#ifndef RENSGA_STRIP_EDITOR_METADATA"] + meta + ["#endif"]
        o += ['      ::rensga::reflect::addMethod<T, &T::%s>(ti, "%s");' % (mn, mn) for mn in t["methods"]]
        if t["trim"] is not None:
            o.append("      ti->trimFloor = %d;" % t["trim"])
        if t["menus"] or t["edit_mode"]:
            o.append("#ifndef RENSGA_STRIP_EDITOR_METADATA")
            if t["edit_mode"]:
                o.append("      ti->executeInEditMode = true;")
            for label, mn in t["menus"]:
                if mn not in t["methods"]:
                    o.append('      ::rensga::reflect::addMethod<T, &T::%s>(ti, "%s");' % (mn, mn))
                o.append('      ::rensga::reflect::addContextMenu(ti, %s, "%s");' % (label, mn))
            o.append("#endif")
        if t["requires"]:
            o.append("#ifndef RENSGA_STRIP_EDITOR_METADATA")
            o += ['      ::rensga::reflect::requireComponent(ti, "%s");' % r for r in t["requires"]]
            o.append("#endif")
        o += ["    }", "  }"]
    o += ["}", "", "namespace {", "const bool rensga_reflect_registrar = (%s(), true);" % fn, "}  // namespace", ""]
    return "\n".join(o)


def write_if_changed(path, data):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            if f.read() == data:
                return
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(data)


def collect(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, names in os.walk(p):
                dirs.sort()
                for n in sorted(names):
                    if n.endswith((".h", ".hpp", ".cpp")):
                        files.append(os.path.join(root, n))
        else:
            files.append(p)
    return files


def run_group(out, group, paths):
    gdir = os.path.join(out, group)
    os.makedirs(gdir, exist_ok=True)
    sources, keep = [], set()
    for p in collect(paths):
        with open(p, "r", encoding="utf-8") as f:
            types = scan_text(f.read(), p)
        is_cpp = p.endswith(".cpp")
        if not types:
            if is_cpp:
                sources.append(os.path.abspath(p))
            continue
        gen = os.path.join(gdir, "%s.reflect.gen.cpp" % ident(p))
        write_if_changed(gen, emit(p, types))
        keep.add(os.path.basename(gen))
        sources.append(os.path.abspath(gen))
    for n in os.listdir(gdir):  # stale outputs
        if n.endswith(".reflect.gen.cpp") and n not in keep:
            os.remove(os.path.join(gdir, n))
    lines = ["# GENERATED by reflect_scan.py", "set(RENSGA_REFLECT_%s_SOURCES" % group.upper()]
    lines += ['  "%s"' % s.replace("\\", "/") for s in sources] + [")", ""]
    write_if_changed(os.path.join(out, group + ".cmake"), "\n".join(lines))


def main(argv):
    out, groups, cur = None, [], None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            out = argv[i + 1]
            i += 1
        elif a == "--group":
            cur = (argv[i + 1], [])
            groups.append(cur)
            i += 1
        elif cur is not None:
            cur[1].append(a)
        else:
            sys.exit("usage: reflect_scan.py --out DIR --group NAME PATH... [--group ...]")
        i += 1
    if not out or not groups:
        sys.exit("usage: reflect_scan.py --out DIR --group NAME PATH... [--group ...]")
    os.makedirs(out, exist_ok=True)
    try:
        for name, paths in groups:
            run_group(out, name, paths)
    except ScanError as e:
        sys.exit("reflect_scan: " + str(e))


if __name__ == "__main__":
    main(sys.argv[1:])
