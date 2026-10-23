"""Real-file check of the member patch engine (chaogarage-members.exe) against mods that were tested in the game.

For every archive member two installed mods share (Shopping.dat and the menu scenes), it makes each mod's patch from the
game's original member and its own payload, then checks that applying the patches gives back:
  1. the mod's own payload (on the original member),
  2. the mod's ready-made 1.2.0 variant over the other mod's payload (on that payload),
  3. the same when the other mod's change is a patch too (both patches on the original member).
"bytes" = identical files; "content" = same entries (scenes: pictures, images, texts, blocks; tables: rows).
Reads the game folder and the mods only; writes below --work. Not part of the release tests (needs the game and the mods).

Usage: python tests\\real\\real_members.py --tool out\\chaogarage-members.exe --game "<game folder>" --work out\\real
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

SCENES = "Resource/SumoToolResources.xpac"
BASE = "Resource/Base.xpac"


def sha(d: bytes) -> str:
    """SHA-256 hex digest."""
    return hashlib.sha256(d).hexdigest()


class Tool:
    """Thin wrapper around chaogarage-members.exe."""

    def __init__(self, exe: Path, work: Path) -> None:
        self.exe, self.work = exe, work

    def run(self, *args: str, ok=(0,)) -> subprocess.CompletedProcess:
        """Run the tool; fail on an unexpected exit code."""
        r = subprocess.run([str(self.exe), *args], capture_output=True, text=True)
        if r.returncode not in ok:
            raise RuntimeError(f"{' '.join(args)}: exit {r.returncode}: {r.stderr.strip()}")
        return r

    def file(self, name: str, data: bytes) -> Path:
        """Write a scratch file and return its path."""
        p = self.work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        return p

    def same(self, fmt: str, a: Path, b: Path) -> bool:
        """Same entries?"""
        return self.run("same", fmt, str(a), str(b), ok=(0, 3)).returncode == 0


def mod_ops(folder: Path) -> dict:
    """(archive, key) -> operation of an installed mod's plan."""
    plan = json.loads((folder / "plan.json").read_text(encoding="utf-8"))
    return {(g["path"], o["key"].upper()): o for g in plan["archive_groups"] for o in g.get("operations", [])}


def verdict(tool: Tool, fmt: str, got: Path, want: bytes, name: str) -> str:
    """bytes / content / DIFFERENT."""
    w = tool.file(name + ".want", want)
    if got.read_bytes() == want:
        return "bytes"
    return "content" if tool.same(fmt, got, w) else "DIFFERENT"


def main() -> int:
    """Run every case and print one line each."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True, type=Path)
    ap.add_argument("--game", required=True, type=Path)
    ap.add_argument("--work", required=True, type=Path)
    a = ap.parse_args()
    if a.work.exists():
        shutil.rmtree(a.work)
    tool = Tool(a.tool.resolve(), a.work.resolve())
    mods = a.game / "mods"
    owners = {"metal-sonic": mods / "Metal Sonic (Roster Pack)", "allstars-roster-pack": mods / "All-Stars Roster Pack"}
    adders = {"egg-hangar": (mods / "Egg Hangar", True), "steam-multiplayer": (mods / "Steam Multiplayer", False)}
    owner_ops = {k: mod_ops(v) for k, v in owners.items()}
    failures = 0
    counts: dict = {}
    for adder, (folder, if_absent) in adders.items():
        for (archive, key), op in sorted(mod_ops(folder).items()):
            for v in op.get("variants", []):
                owner = v.get("over_mod")
                oop = owner_ops[owner].get((archive, key))
                if not oop or sha((owners[owner] / oop["payload"]).read_bytes()) != v["over_payload_sha256"]:
                    continue
                fmt = "ssrt" if archive == BASE else "scene"
                tag = f"{adder}/{key}/{owner}"
                stock = tool.file(f"{tag}/stock", b"")
                tool.run("extract", str(a.game / archive.replace("/", "\\")), key, str(stock))
                own = (folder / op["payload"]).read_bytes()
                other = (owners[owner] / oop["payload"]).read_bytes()
                variant = (folder / v["payload"]).read_bytes()
                ownp, otherp = tool.file(f"{tag}/own", own), tool.file(f"{tag}/other", other)
                patch = a.work / tag / "patch" / "patch.json"
                patch.parent.mkdir(parents=True, exist_ok=True)
                tool.run("make", fmt, str(stock), str(ownp), str(patch), *(["--if-absent"] if if_absent else []))
                opatch = a.work / tag / "opatch" / "patch.json"
                opatch.parent.mkdir(parents=True, exist_ok=True)
                tool.run("make", fmt, str(stock), str(otherp), str(opatch))
                level = ["--level", "9"] if fmt == "scene" else []
                out1, out2, out3 = (a.work / tag / n for n in ("on-stock", "on-other", "both-patches"))
                tool.run("apply", fmt, str(stock), str(out1), *level, str(patch))
                tool.run("apply", fmt, str(otherp), str(out2), *level, str(patch))
                notes = tool.run("apply", fmt, str(stock), str(out3), *level, str(opatch), str(patch)).stdout.strip()
                r = (verdict(tool, fmt, out1, own, f"{tag}/v1"), verdict(tool, fmt, out2, variant, f"{tag}/v2"),
                     verdict(tool, fmt, out3, variant, f"{tag}/v3"))
                failures += "DIFFERENT" in r
                counts[r] = counts.get(r, 0) + 1
                name = op.get("name", "")
                print(f"{tag:40} own:{r[0]:9} variant:{r[1]:9} both-patches:{r[2]:9} {name[-40:]}" + (f"\n    {notes}" if notes else ""))
    print("summary:", {" / ".join(k): v for k, v in counts.items()}, "failures:", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
