#!/usr/bin/env python3
"""Run the common package gate, also proving the new preview exports exist."""
import importlib.util
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "BOOT-02" / "check_package.py"
spec = importlib.util.spec_from_file_location("common_package_gate", path)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
checker.CONTROLS = tuple(dict.fromkeys((*checker.CONTROLS,
    "voxy_salvage_preview_action", "voxy_get_salvage_preview_json")))

if __name__ == "__main__":
    checker.main()
