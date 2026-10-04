#!/usr/bin/env python3
"""Runs an SPRT test of a dev build against a base build of ChessEngine.

Both sides are built from git refs (cached by commit), fingerprinted with the engine's "bench"
command, and played against each other with fastchess (preferred) or cutechess-cli using an
opening book. At the end a Markdown summary is printed for pasting into the pull request.

Examples:
    tools/sprt.py                                  # HEAD vs main, gainer bounds, STC
    tools/sprt.py --test simplification --tc ltc   # non-regression test at long time control
    tools/sprt.py --base v0.1.0 --dev my-branch --concurrency 6

See docs/sprt.md for the full workflow. Uses only the Python standard library.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EXE_SUFFIX = ".exe" if platform.system() == "Windows" else ""

# Time control presets, as "seconds+increment" per game.
TIME_CONTROLS = {
    "stc": "8+0.08",
    "ltc": "40+0.4",
}

# SPRT bounds presets, in normalized Elo when fastchess runs the test.
# gainer:          the change must be shown to gain strength.
# simplification:  the change must be shown not to lose strength (non-regression).
BOUNDS = {
    "gainer": (0.0, 5.0),
    "simplification": (-5.0, 0.0),
}

DEFAULT_BOOK_NAME = "UHO_Lichess_4852_v1.epd"
DEFAULT_BOOK_URL = (
    "https://raw.githubusercontent.com/official-stockfish/books/master/"
    "UHO_Lichess_4852_v1.epd.zip"
)

BENCH_LINE = re.compile(r"^(\d+) nodes (\d+) nps\s*$", re.MULTILINE)


@dataclass
class Side:
    label: str  # "dev" or "base"
    ref: str  # git ref as given, or the binary path
    commit: str  # full commit hash, or "" for a prebuilt binary
    binary: Path
    bench_nodes: int | None = None
    bench_nps: int | None = None

    @property
    def short(self) -> str:
        return self.commit[:10] if self.commit else self.binary.name

    @property
    def engine_name(self) -> str:
        return f"{self.label}-{self.short}"


def log(message: str) -> None:
    print(f"[sprt] {message}", flush=True)


def fail(message: str) -> None:
    print(f"[sprt] error: {message}", file=sys.stderr)
    sys.exit(1)


def git(*args: str) -> str:
    result = subprocess.run(
        ["git", *args], cwd=REPO_ROOT, check=True, capture_output=True, text=True
    )
    return result.stdout.strip()


def resolve_commit(ref: str) -> str:
    # A fresh clone may only have origin/main, so fall back to the remote-tracking branch.
    for candidate in (ref, f"origin/{ref}"):
        try:
            return git("rev-parse", "--verify", "--quiet", f"{candidate}^{{commit}}")
        except subprocess.CalledProcessError:
            continue
    fail(f"cannot resolve git ref '{ref}'")
    raise AssertionError  # unreachable


def find_built_binary(build_dir: Path) -> Path:
    name = f"chessengine{EXE_SUFFIX}"
    for candidate in (build_dir / "src" / name, build_dir / "src" / "Release" / name):
        if candidate.is_file():
            return candidate
    matches = sorted(build_dir.rglob(name))
    if not matches:
        fail(f"build finished but no {name} found under {build_dir}")
    return matches[0]


def build_commit(commit: str, work_dir: Path, jobs: int | None) -> Path:
    """Builds a release binary of `commit`, reusing a cached one when present."""
    binary = work_dir / "bin" / commit / f"chessengine{EXE_SUFFIX}"
    if binary.is_file():
        log(f"using cached build of {commit[:10]}")
        return binary

    log(f"building {commit[:10]} (release)")
    with tempfile.TemporaryDirectory(prefix="sprt-") as tmp:
        tmp_path = Path(tmp)
        source = tmp_path / "src"
        build = tmp_path / "build"
        git("worktree", "add", "--detach", str(source), commit)
        try:
            configure = [
                "cmake", "-S", str(source), "-B", str(build),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCHESS_BUILD_TESTS=OFF",
                "-DCHESS_WARNINGS_AS_ERRORS=OFF",
            ]
            if shutil.which("ninja"):
                configure += ["-G", "Ninja"]
            subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
            build_cmd = ["cmake", "--build", str(build), "--config", "Release"]
            if jobs:
                build_cmd += ["--parallel", str(jobs)]
            subprocess.run(build_cmd, check=True, stdout=subprocess.DEVNULL)
            binary.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(find_built_binary(build), binary)
        finally:
            git("worktree", "remove", "--force", str(source))
    return binary


def run_bench(side: Side) -> None:
    result = subprocess.run(
        [str(side.binary), "bench"], check=True, capture_output=True, text=True, timeout=600
    )
    matches = BENCH_LINE.findall(result.stdout)
    if not matches:
        # Builds that predate the bench command still play; they just have no fingerprint.
        log(f"warning: {side.label} {side.short} has no usable 'bench' command")
        return
    nodes, nps = matches[-1]
    side.bench_nodes, side.bench_nps = int(nodes), int(nps)
    log(f"{side.label} {side.short}: bench {side.bench_nodes} nodes, {side.bench_nps} nps")


def ensure_book(path: Path | None, work_dir: Path) -> Path:
    if path is not None:
        if not path.is_file():
            fail(f"opening book not found: {path}")
        return path

    book = work_dir / "books" / DEFAULT_BOOK_NAME
    if book.is_file():
        return book
    log(f"downloading opening book {DEFAULT_BOOK_NAME}")
    book.parent.mkdir(parents=True, exist_ok=True)
    archive = book.with_suffix(".epd.zip")
    for attempt in range(1, 4):
        try:
            with urllib.request.urlopen(DEFAULT_BOOK_URL, timeout=60) as response:
                with archive.open("wb") as file:
                    shutil.copyfileobj(response, file)
            break
        except OSError as error:
            if attempt == 3:
                fail(f"could not download {DEFAULT_BOOK_URL}: {error}; pass --book instead")
            log(f"download failed ({error}), retrying")
    with zipfile.ZipFile(archive) as zipped:
        zipped.extract(DEFAULT_BOOK_NAME, book.parent)
    archive.unlink()
    return book


def find_runner(requested: str, runner_path: str | None) -> tuple[str, str]:
    """Returns (kind, executable) where kind is 'fastchess' or 'cutechess'."""
    if runner_path:
        exe = runner_path
        kind = requested
        if kind == "auto":
            kind = "cutechess" if "cutechess" in Path(runner_path).name else "fastchess"
        return kind, exe

    candidates = {"fastchess": ["fastchess"], "cutechess": ["cutechess-cli", "cutechess"]}
    order = ["fastchess", "cutechess"] if requested == "auto" else [requested]
    for kind in order:
        for name in candidates[kind]:
            exe = shutil.which(name)
            if exe:
                return kind, exe
    fail(
        "no match runner (fastchess or cutechess-cli) found; install fastchess (https://github.com/Disservin/fastchess) "
        "or cutechess-cli, put it on PATH or pass --runner-path"
    )
    raise AssertionError  # unreachable


def runner_command(
    kind: str, exe: str, dev: Side, base: Side, args: argparse.Namespace,
    book: Path, elo0: float, elo1: float, tc: str, pgn: Path,
) -> list[str]:
    def engine(side: Side) -> list[str]:
        return ["-engine", f"cmd={side.binary}", f"name={side.engine_name}"]

    each = ["-each", "proto=uci", f"tc={tc}"]
    each += [f"option.{option}" for option in args.engine_option]

    cmd = [exe, *engine(dev), *engine(base), *each]
    cmd += ["-openings", f"file={book}", f"format={book.suffix.lstrip('.')}", "order=random"]
    cmd += ["-games", "2", "-rounds", str(args.rounds), "-repeat", "-recover"]
    cmd += ["-concurrency", str(args.concurrency)]
    cmd += ["-draw", "movenumber=40", "movecount=8", "score=10"]
    cmd += ["-resign", "movecount=3", "score=600", "twosided=true"]
    cmd += ["-ratinginterval", "10"]
    sprt = ["-sprt", f"elo0={elo0}", f"elo1={elo1}", f"alpha={args.alpha}", f"beta={args.beta}"]
    if kind == "fastchess":
        sprt.append(f"model={args.model}")
        cmd += ["-pgnout", f"file={pgn}"]
    else:
        cmd += ["-pgnout", str(pgn)]
    return cmd + sprt


def extract_result(kind: str, output: str) -> str:
    """Pulls the final statistics block out of the runner's output."""
    lines = output.splitlines()
    if kind == "fastchess":
        starts = [i for i, line in enumerate(lines) if line.startswith("Results of ")]
        if not starts:
            return ""
        block = []
        for line in lines[starts[-1]:]:
            if line.startswith("---"):
                break
            block.append(line)
        verdicts = [line for line in lines if re.search(r"SPRT .* completed", line)]
        return "\n".join(block + verdicts[-1:])

    wanted = ("Score of ", "Elo difference:", "SPRT:")
    picked = []
    for prefix in wanted:
        hits = [line.strip() for line in lines if line.strip().startswith(prefix)]
        if hits:
            picked.append(hits[-1])
    return "\n".join(picked)


def verdict(result: str) -> str:
    if "H1 was accepted" in result:
        return "passed (H1 accepted)"
    if "H0 was accepted" in result:
        return "failed (H0 accepted)"
    return "inconclusive (stopped before a decision)"


def markdown_summary(
    dev: Side, base: Side, test: str, tc: str, elo0: float, elo1: float,
    args: argparse.Namespace, kind: str, book: Path, result: str,
) -> str:
    model = args.model if kind == "fastchess" else "cutechess-cli default"
    return "\n".join([
        "### SPRT",
        "",
        "| | |",
        "| --- | --- |",
        f"| Result | **{verdict(result)}** |",
        f"| Test | {test} |",
        f"| Bounds | [{elo0:.2f}, {elo1:.2f}] ({model}), alpha={args.alpha}, beta={args.beta} |",
        f"| Time control | {tc} |",
        f"| Opening book | {book.name} |",
        f"| Dev | `{dev.short}`, bench {dev.bench_nodes or 'n/a'} |",
        f"| Base | `{base.short}`, bench {base.bench_nodes or 'n/a'} |",
        f"| Runner | {kind}, concurrency {args.concurrency} |",
        "",
        "```text",
        result or "(no result block found in the runner output; paste it here by hand)",
        "```",
    ])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--dev", default="HEAD", help="git ref of the build under test")
    parser.add_argument("--base", default="main", help="git ref to test against")
    parser.add_argument("--dev-bin", type=Path, help="prebuilt dev engine (skips building --dev)")
    parser.add_argument("--base-bin", type=Path, help="prebuilt base engine (skips building --base)")
    parser.add_argument("--test", choices=sorted(BOUNDS), default="gainer",
                        help="which bounds preset to use")
    parser.add_argument("--elo0", type=float, help="override the lower SPRT bound")
    parser.add_argument("--elo1", type=float, help="override the upper SPRT bound")
    parser.add_argument("--alpha", type=float, default=0.05)
    parser.add_argument("--beta", type=float, default=0.05)
    parser.add_argument("--model", choices=["normalized", "logistic", "bayesian"],
                        default="normalized", help="Elo model of the bounds (fastchess only)")
    parser.add_argument("--tc", default="stc",
                        help=f"time control: a preset {sorted(TIME_CONTROLS)} or e.g. 10+0.1")
    parser.add_argument("--book", type=Path,
                        help=f"opening book (.epd or .pgn); default downloads {DEFAULT_BOOK_NAME}")
    parser.add_argument("--concurrency", type=int, default=max(1, (os.cpu_count() or 2) - 1),
                        help="games played in parallel")
    parser.add_argument("--rounds", type=int, default=20000,
                        help="maximum game pairs before stopping without a decision")
    parser.add_argument("--engine-option", action="append", default=[], metavar="NAME=VALUE",
                        help="UCI option for both engines, e.g. Hash=16 (repeatable)")
    parser.add_argument("--runner", choices=["auto", "fastchess", "cutechess"], default="auto")
    parser.add_argument("--runner-path", help="path to the match runner executable")
    parser.add_argument("--jobs", type=int, help="parallel build jobs")
    parser.add_argument("--work-dir", type=Path, default=REPO_ROOT / ".sprt",
                        help="cache for builds, books and results")
    parser.add_argument("--dry-run", action="store_true",
                        help="build and bench both sides, print the runner command, don't play")
    return parser.parse_args()


def prepare_side(label: str, ref: str, prebuilt: Path | None, args: argparse.Namespace) -> Side:
    if prebuilt is not None:
        if not prebuilt.is_file():
            fail(f"{label} binary not found: {prebuilt}")
        return Side(label, str(prebuilt), "", prebuilt.resolve())
    commit = resolve_commit(ref)
    return Side(label, ref, commit, build_commit(commit, args.work_dir, args.jobs))


def main() -> None:
    args = parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)

    elo0, elo1 = BOUNDS[args.test]
    elo0 = args.elo0 if args.elo0 is not None else elo0
    elo1 = args.elo1 if args.elo1 is not None else elo1
    if elo0 >= elo1:
        fail(f"elo0 ({elo0}) must be below elo1 ({elo1})")
    tc = TIME_CONTROLS.get(args.tc, args.tc)

    if args.dev_bin is None and args.dev == "HEAD" and git("status", "--porcelain", "-uno"):
        log("warning: uncommitted changes are NOT part of the dev build; commit them first")

    kind, exe = find_runner(args.runner, args.runner_path)
    dev = prepare_side("dev", args.dev, args.dev_bin, args)
    base = prepare_side("base", args.base, args.base_bin, args)
    if dev.commit and dev.commit == base.commit:
        log("warning: dev and base are the same commit")
    run_bench(dev)
    run_bench(base)
    book = ensure_book(args.book, args.work_dir)

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = args.work_dir / "results" / f"{stamp}-{dev.short}-vs-{base.short}"
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = runner_command(kind, exe, dev, base, args, book, elo0, elo1, tc,
                         out_dir / "games.pgn")
    (out_dir / "command.txt").write_text(" ".join(cmd) + "\n")
    log(f"{args.test} test, bounds [{elo0}, {elo1}], tc {tc}, {kind}")
    log("command: " + " ".join(cmd))
    if args.dry_run:
        return

    output_lines = []
    # Run inside the results directory: fastchess autosaves its state (config.json) to the cwd.
    with subprocess.Popen(cmd, cwd=out_dir, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, bufsize=1) as process:
        assert process.stdout is not None
        try:
            for line in process.stdout:
                sys.stdout.write(line)
                output_lines.append(line)
        except KeyboardInterrupt:
            log("interrupted; summarizing the games played so far")
            process.terminate()
    output = "".join(output_lines)
    (out_dir / "runner.log").write_text(output)

    summary = markdown_summary(dev, base, args.test, tc, elo0, elo1, args, kind, book,
                               extract_result(kind, output))
    (out_dir / "summary.md").write_text(summary + "\n")
    print()
    print(summary)
    print()
    shown = out_dir.relative_to(REPO_ROOT) if out_dir.is_relative_to(REPO_ROOT) else out_dir
    log(f"results saved in {shown}")


if __name__ == "__main__":
    main()
