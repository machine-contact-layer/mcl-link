#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "registries" / "transport-ids-v0.1.json"


def main():
    reg = json.loads(REGISTRY.read_text(encoding="utf-8"))
    assert reg["field_width_bits"] == 8

    ids = [a["id"] for a in reg["assignments"]]
    names = [a["name"] for a in reg["assignments"]]
    owners = [a["owner_repo"] for a in reg["assignments"]]

    assert len(ids) == len(set(ids))
    assert len(names) == len(set(names))
    assert len(owners) == len(set(owners))
    assert all(1 <= i <= 0x7F for i in ids)

    print(f"transport assignments: {len(ids)}")
    print("OK")


if __name__ == "__main__":
    main()
