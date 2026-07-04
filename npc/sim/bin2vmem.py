#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} input.bin output.hex", file=sys.stderr)
        return 1

    data = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)

    with out.open("w", encoding="ascii") as f:
        for i in range(0, len(data), 4):
            word = data[i : i + 4].ljust(4, b"\0")
            f.write(f"{int.from_bytes(word, 'little'):08x}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
