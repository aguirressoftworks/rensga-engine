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
}


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
    r"|(?P<mac>\b(?P<macname>REFLECT|SERIALIZE_FIELD|RANGE|HEADER|TOOLTIP|REQUIRE_COMPONENT)\s*\()"
    r"|(?P<open>\{)|(?P<close>\})"
)


def scan_text(raw, fname):
    text = mask(raw)
    types, stack, pending = [], [], None
    pos = 0
    pending = {"header": None, "tooltip": None, "range": None}

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
                types.append({"name": args[0], "qual": "::" + "::".join(q), "fields": [], "requires": [],
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
                pending = {"header": None, "tooltip": None, "range": None}
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
         '#include "rensga/core/reflect.h"', "",
         "void %s() {" % fn,
         "  static bool done = false;",
         "  if (done) return;",
         "  done = true;"]
    for t in types:
        o += ["  {", "    using T = %s;" % t["qual"],
              '    ::rensga::TypeInfo* ti = ::rensga::reflect::beginType<T>("%s");' % t["name"],
              "    if (ti) {", "      ::rensga::FieldDesc* f = nullptr;", "      (void)f;"]
        for fd in t["fields"]:
            o.append('      f = ::rensga::reflect::addField(ti, "%s", ::rensga::FieldType::%s, &T::%s);'
                     % (fd["name"], fd["type"], fd["name"]))
            meta = []
            if fd["header"]:
                meta.append("      f->header = %s;" % fd["header"])
            if fd["tooltip"]:
                meta.append("      f->tooltip = %s;" % fd["tooltip"])
            if fd["range"]:
                meta.append("      f->hasRange = true;")
                meta.append("      f->rangeMin = static_cast<::rensga::Scalar>(%s);" % fd["range"][0])
                meta.append("      f->rangeMax = static_cast<::rensga::Scalar>(%s);" % fd["range"][1])
            if meta:
                o += ["#ifndef RENSGA_STRIP_EDITOR_METADATA"] + meta + ["#endif"]
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
