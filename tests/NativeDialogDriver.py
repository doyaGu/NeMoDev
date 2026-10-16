"""Answer only the named regression dialog through GNOME's accessibility API."""
import argparse
from pathlib import Path
import time
import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


def descendants(node, depth=0):
    if depth > 45:
        return
    yield node
    for i in range(node.get_child_count()):
        child = node.get_child_at_index(i)
        if child:
            yield from descendants(child, depth + 1)


def find_window(title):
    desktop = Atspi.get_desktop(0)
    for i in range(desktop.get_child_count()):
        app = desktop.get_child_at_index(i)
        for j in range(app.get_child_count()):
            window = app.get_child_at_index(j)
            if window and window.get_name() == title:
                return window
    return None


def action(node, name):
    actions = node.get_action_iface()
    if actions:
        for i in range(actions.get_n_actions()):
            if actions.get_action_name(i) == name:
                return actions.do_action(i)
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", required=True)
    parser.add_argument("--action", choices=("select", "cancel"), required=True)
    parser.add_argument("--name", default="")
    parser.add_argument("--ready", type=Path)
    parser.add_argument("--gate", type=Path)
    args = parser.parse_args()
    deadline = time.monotonic() + 50
    answered = False
    while time.monotonic() < deadline:
        try:
            window = find_window(args.title)
            if not window:
                if answered:
                    print("Native dialog closed", flush=True)
                    return 0
                time.sleep(.05)
                continue
            if args.ready:
                args.ready.write_text("shown", encoding="utf-8")
            if args.gate and not args.gate.exists():
                time.sleep(.05)
                continue
            if args.action == "cancel":
                answered = action(window, "window.close")
                if not answered:
                    for node in descendants(window):
                        if node.get_role_name() == "push button" and node.get_name() == "Cancel":
                            answered = action(node, "click")
                            break
            else:
                nodes = list(descendants(window))
                cell = next((n for n in nodes if n.get_role_name() in ("table cell", "list item") and
                             n.get_name() in (args.name, args.name + ". File")), None)
                if cell:
                    parent = cell.get_parent()
                    selection = parent.get_selection_iface()
                    if not selection or not selection.select_child(cell.get_index_in_parent()):
                        raise RuntimeError("Cannot select the regression file")
                    for node in nodes:
                        if node.get_name() in ("Select", "Open") and node.get_role_name() in ("push button", "button"):
                            if node.get_state_set().contains(Atspi.StateType.SENSITIVE):
                                answered = action(node, "click")
                                break
            time.sleep(.05)
        except GLib.Error:
            # The portal may replace its loading tree, or destroy the frame
            # between selection and the next accessibility call.
            time.sleep(.05)
    window = find_window(args.title)
    if window:
        action(window, "window.close")
    raise RuntimeError("Timed out answering the regression dialog: " + args.title)


if __name__ == "__main__":
    raise SystemExit(main())
