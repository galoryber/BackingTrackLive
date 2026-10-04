#!/usr/bin/env python3
"""Flags two ImGui footguns that a screenshot cannot show.

ONE: two widgets that can be visible at once with the same ID.

ImGui derives a widget's identity from its label. Two visible widgets sharing
one are genuinely ambiguous - clicks land on whichever ImGui guessed - and the
symptom is a control that silently does nothing. v0.1.8 shipped exactly that:
a transport "stop" button beside the on_end "stop" radio button. ImGui detects
this at runtime, but only while the item is hovered, so a headless render
never sees it and neither does a screenshot.

The fix is to give one an explicit id: "stop##transport".

TWO: SetKeyboardFocusHere() called unconditionally.

It is meant for the frame a field appears on. Called every frame it forces
ImGui's active item back to that field continuously, and a button needs to
hold the active item from press to release - so every button on the screen
silently stops working. v0.1.15 shipped a name prompt that did this: Create
did nothing, and neither did Cancel.

The fix is to guard it with a flag you clear after taking focus.
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
FOCUS = re.compile(r'ImGui::SetKeyboardFocusHere\s*\(')
IFSTMT = re.compile(r'\bif\s*\(')


def check_focus(path, lines):
    bad = 0
    for n, line in enumerate(lines, 1):
        if not FOCUS.search(line):
            continue
        # Guarded if an `if` opens within the three lines above it.
        if any(IFSTMT.search(w) for w in lines[max(0, n - 4):n - 1]):
            continue
        print(f"{path}:{n}: SetKeyboardFocusHere() looks unguarded - called "
              f"every frame it stops every button on the screen working")
        bad += 1
    return bad


def check_ids(path, lines):
    bad = 0
    seen = {}
    for n, line in enumerate(lines, 1):
        # A new function starts a new scope. Detected at any brace depth,
        # because these files put everything inside `namespace { }`.
        if FUNC.match(line.strip()):
            seen = {}
        for m in CALL.finditer(line):
            label = m.group(2)
            if "##" in label or label == "":
                continue          # already disambiguated, or an id-less label
            if label in seen:
                print(f'{path}:{n}: "{label}" ({m.group(1)}) shares its ID '
                      f'with {path}:{seen[label][0]} ({seen[label][1]}) '
                      f'in the same function')
                bad += 1
            else:
                seen[label] = (n, m.group(1))
    return bad


def main(paths):
    bad = 0
    for path in paths:
        lines = pathlib.Path(path).read_text(
            encoding="utf-8", errors="replace").splitlines()
        bad += check_ids(path, lines)
        bad += check_focus(path, lines)
    if bad:
        print(f"\n{bad} problem(s) found.")
        return 1
    print(f"no conflicting ImGui IDs or unguarded focus calls "
          f"in {len(paths)} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
