#!/usr/bin/env python3
"""Flags two ImGui widgets that can be visible at once with the same ID.

ImGui derives a widget's identity from its label. Two visible widgets sharing
one are genuinely ambiguous - clicks land on whichever ImGui guessed - and the
symptom is a control that silently does nothing. v0.1.8 shipped exactly that:
a transport "stop" button beside the on_end "stop" radio button.

ImGui detects this at runtime, but only while the item is hovered, so a
headless render never sees it and neither does a screenshot. Hence a static
check: within one function, the same literal label passed twice to widgets
that take their ID from the label.

The fix is always to give one an explicit id: "stop##transport".
"""
import re, sys, pathlib

# Widgets whose ID comes from the label. Ones that push their own scope
# (TreeNode, BeginChild) or take an explicit id are not listed.
WIDGETS = ("Button", "SmallButton", "InvisibleButton", "RadioButton",
           "Checkbox", "Selectable", "InputText", "InputInt", "InputFloat",
           "InputDouble", "SliderFloat", "SliderInt", "DragFloat", "DragInt",
           "BeginCombo", "ColorEdit3", "ColorEdit4", "ArrowButton")
CALL = re.compile(r'ImGui::(' + "|".join(WIDGETS) + r')\(\s*"([^"]*)"')
FUNC = re.compile(r'^[A-Za-z_][\w:<>,&* ]*\s[\w:]+\s*\([^;]*\)\s*\{\s*$')

def main(paths):
    bad = 0
    for path in paths:
        text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
        # A new function starts a new scope. Detected at any brace depth,
        # because these files put everything inside `namespace { }`.
        seen = {}
        for n, line in enumerate(text.splitlines(), 1):
            if FUNC.match(line.strip()):
                seen = {}
            for m in CALL.finditer(line):
                label = m.group(2)
                if "##" in label or label == "":
                    continue          # already disambiguated, or an id-less label
                key = label
                if key in seen:
                    print(f"{path}:{n}: \"{label}\" ({m.group(1)}) shares its ID "
                          f"with {path}:{seen[key][0]} ({seen[key][1]}) "
                          f"in the same function")
                    bad += 1
                else:
                    seen[key] = (n, m.group(1))
    if bad:
        print(f"\n{bad} conflicting ID(s). Give one an explicit id, "
              f'e.g. "stop##transport".')
        return 1
    print(f"no conflicting ImGui IDs in {len(paths)} file(s)")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
