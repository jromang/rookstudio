#!/usr/bin/env python3
"""The facts' types, written in C++ from the Rust prototype's `enum Fact` (crates/facts/src/lib.rs).

    tools/port/gen_facts.py ~/repos/rookstudio-rust

writes commentary/src/commentary/fact_types.hpp and fact_types.cpp: one struct per fact with its fields and their
comments, the variant of them all, each fact's kind name, and the debug text Rust's derive(Debug) gives, which the
reference briefs quote. What the facts mean and weigh (facts.cpp) is ported by hand.
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# Rust types to C++ ones; Option and Vec are handled around them.
TYPES = {
    "Placed": "Placed",
    "Color": "Color",
    "i32": "int32_t",
    "u32": "uint32_t",
    "bool": "bool",
    "Move": "Move",
    "Trade": "Trade",
    "Lever": "Lever",
    "Verdict": "Verdict",
    "Formation": "Formation",
    "Ending": "Ending",
    "MatePattern": "MatePattern",
    "Wing": "Wing",
    "LineKind": "LineKind",
    "CastlingSide": "CastlingSide",
    "File": "File",
    "Role": "Role",
    "Square": "Square",
}


def cxx_type(rust: str) -> str:
    rust = rust.strip()
    m = re.fullmatch(r"Option<(.+)>", rust)
    if m:
        return f"std::optional<{cxx_type(m.group(1))}>"
    m = re.fullmatch(r"Vec<(.+)>", rust)
    if m:
        return f"std::vector<{cxx_type(m.group(1))}>"
    if rust not in TYPES:
        sys.exit(f"no C++ type for {rust}")
    cxx = TYPES[rust]
    # Facts named like a type (Formation, Castles): the type taken from the namespace around them.
    return cxx if cxx in ("int32_t", "uint32_t", "bool") else "commentary::" + cxx


def parse(lib: str):
    body = lib[lib.index("pub enum Fact {") + len("pub enum Fact {") :]
    body = body[: body.index("\n}\n")]
    variants, doc = [], []
    lines = body.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line:
            continue
        if line.startswith("///"):
            doc.append(line[3:].strip())
            continue
        m = re.fullmatch(r"([A-Z][A-Za-z]*) \{", line)
        if not m:
            sys.exit(f"unexpected line in enum Fact: {line}")
        name, fields, field_doc = m.group(1), [], []
        while True:
            line = lines[i].strip()
            i += 1
            if line == "},":
                break
            if line.startswith("///"):
                field_doc.append(line[3:].strip())
                continue
            fm = re.fullmatch(r"([a-z_]+): (.+),", line)
            if not fm:
                sys.exit(f"unexpected field in {name}: {line}")
            fields.append((fm.group(1), fm.group(2), field_doc))
            field_doc = []
        variants.append((name, fields, doc))
        doc = []
    return variants


def comment(lines, indent):
    return "".join(f"{indent}// {line}\n" for line in lines)


def main():
    rust = pathlib.Path(sys.argv[1]).expanduser() if len(sys.argv) > 1 else pathlib.Path.home() / "repos/rookstudio-rust"
    lib = (rust / "crates/facts/src/lib.rs").read_text()
    variants = parse(lib)

    hpp = [
        "#pragma once\n",
        "\n",
        "// Written by tools/port/gen_facts.py from the Rust prototype's enum Fact (crates/facts/src/lib.rs): do not edit.\n",
        "// The facts the detectors find, which the comments may speak of and nothing else.\n",
        "\n",
        "#include <array>\n#include <cstdint>\n#include <optional>\n#include <string>\n#include <variant>\n#include <vector>\n",
        "\n",
        '#include "fact_parts.hpp"\n',
        "\n",
        "namespace fcg::commentary {\n",
        "\n",
        "namespace fact {\n",
    ]
    for name, fields, doc in variants:
        hpp.append("\n")
        hpp.append(comment(doc, ""))
        hpp.append(f"struct {name} {{\n")
        for field, rtype, fdoc in fields:
            hpp.append(comment(fdoc, "    "))
            default = " = false" if rtype == "bool" else (" = 0" if rtype in ("i32", "u32") else "{}")
            hpp.append(f"    {cxx_type(rtype)} {field}{default};\n")
        hpp.append(f"    bool operator==(const {name}&) const = default;\n")
        hpp.append("};\n")
    hpp.append("\n}  // namespace fact\n\n")
    hpp.append("using Fact = std::variant<\n")
    hpp.append(",\n".join(f"    fact::{name}" for name, _, _ in variants))
    hpp.append(">;\n\n")
    hpp.append("// The fact's kind, its variant's name in the prototype: \"Fork\", \"WinsMaterial\".\n")
    hpp.append("[[nodiscard]] const char* kindName(const Fact& fact) noexcept;\n")
    hpp.append("// Rust's debug text of the fact: \"Develops { piece: Placed { color: White, role: Bishop, square: D3 } }\".\n")
    hpp.append("[[nodiscard]] std::string debug(const Fact& fact);\n")
    hpp.append("\n}  // namespace fcg::commentary\n")

    cpp = [
        "// Written by tools/port/gen_facts.py from the Rust prototype's enum Fact (crates/facts/src/lib.rs): do not edit.\n",
        "\n",
        '#include "fact_types.hpp"\n',
        "\n",
        "namespace fcg::commentary {\n",
        "\n",
        "namespace {\n",
        "\n",
        "template <class... F>\n",
        "struct Overloaded : F... {\n    using F::operator()...;\n};\n",
        "template <class... F>\n",
        "Overloaded(F...) -> Overloaded<F...>;\n",
        "\n",
        "}  // namespace\n",
        "\n",
        "const char* kindName(const Fact& fact) noexcept {\n",
        "    return std::visit(Overloaded{\n",
    ]
    cpp.append(",\n".join(f"        [](const fact::{name}&) {{ return \"{name}\"; }}" for name, _, _ in variants))
    cpp.append("}, fact);\n}\n\n")
    cpp.append("std::string debug(const Fact& fact) {\n")
    cpp.append("    return std::visit(Overloaded{\n")
    parts = []
    for name, fields, _ in variants:
        pieces = " + \", \" + ".join(f"\"{field}: \" + debugValue(f.{field})" for field, _, _ in fields)
        parts.append(f"        [](const fact::{name}& f) {{ return std::string(\"{name} {{ \") + {pieces} + \" }}\"; }}")
    cpp.append(",\n".join(parts))
    cpp.append("}, fact);\n}\n\n}  // namespace fcg::commentary\n")

    out = ROOT / "commentary/src/commentary"
    (out / "fact_types.hpp").write_text("".join(hpp))
    (out / "fact_types.cpp").write_text("".join(cpp))
    # In the repository's style, as committed.
    subprocess.run(["clang-format", "-i", str(out / "fact_types.hpp"), str(out / "fact_types.cpp")], check=True)
    print(f"{len(variants)} facts")


if __name__ == "__main__":
    main()
