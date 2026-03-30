#!/usr/bin/env python3
"""
Continuous agent supervisor for TuringOS.

Runs each configured agent command in a loop and auto-commits progress updates
using task IDs completed in `.cursor/progress.md`.
"""

from __future__ import annotations

import argparse
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


TASK_RE = re.compile(r"^- \[x\]\s+(P\d+-\d+)\b", re.IGNORECASE)


@dataclass
class Agent:
    name: str
    command: str


def read_agents(config_path: Path) -> list[Agent]:
    agents: list[Agent] = []
    for raw_line in config_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        if ":" in line:
            name, command = line.split(":", 1)
            name = name.strip()
            command = command.strip()
            if not name or not command:
                continue
            agents.append(Agent(name=name, command=command))
            continue

        agents.append(Agent(name=f"agent-{len(agents) + 1}", command=line))

    return agents


def read_completed_task_ids(progress_path: Path) -> set[str]:
    completed: set[str] = set()
    for line in progress_path.read_text(encoding="utf-8").splitlines():
        match = TASK_RE.match(line.strip())
        if match:
            completed.add(match.group(1).upper())
    return completed


def run_shell(command: list[str], cwd: Path, check: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        text=True,
        capture_output=True,
        check=check,
    )


def working_tree_has_changes(repo_root: Path) -> bool:
    result = run_shell(["git", "status", "--porcelain"], cwd=repo_root)
    return bool(result.stdout.strip())


def format_commit_message(task_ids: Iterable[str]) -> str:
    ids = sorted(set(task_ids))
    grouped: dict[str, list[str]] = {}

    for task_id in ids:
        phase, suffix = task_id.split("-", 1)
        grouped.setdefault(phase, []).append(suffix)

    parts: list[str] = []
    for phase in sorted(grouped.keys()):
        suffixes = sorted(grouped[phase])
        if not suffixes:
            continue
        if len(suffixes) == 1:
            parts.append(f"{phase}-{suffixes[0]}")
        else:
            parts.append(f"{phase}-{','.join(suffixes)}")

    return ",".join(parts)


def commit_progress(repo_root: Path, commit_message: str, push: bool) -> bool:
    run_shell(["git", "add", "-A"], cwd=repo_root, check=True)
    has_staged = run_shell(["git", "diff", "--cached", "--name-only"], cwd=repo_root, check=True)
    if not has_staged.stdout.strip():
        return False

    run_shell(["git", "commit", "-m", commit_message], cwd=repo_root, check=True)
    if push:
        run_shell(["git", "push"], cwd=repo_root, check=True)
    return True


def run_agent(agent: Agent, repo_root: Path) -> int:
    print(f"\n=== Running {agent.name} ===")
    print(f"$ {agent.command}")
    proc = subprocess.run(agent.command, cwd=str(repo_root), shell=True)
    print(f"=== {agent.name} exit={proc.returncode} ===")
    return proc.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Run configured agents continuously.")
    parser.add_argument(
        "--repo-root",
        default=".",
        help="Path to git repository root (default: current directory).",
    )
    parser.add_argument(
        "--config",
        default=".cursor/agents.txt",
        help="Agent config file (default: .cursor/agents.txt).",
    )
    parser.add_argument(
        "--progress",
        default=".cursor/progress.md",
        help="Progress markdown path (default: .cursor/progress.md).",
    )
    parser.add_argument(
        "--sleep-seconds",
        type=float,
        default=2.0,
        help="Sleep between agent runs (default: 2.0).",
    )
    parser.add_argument(
        "--push",
        action="store_true",
        help="Push after each successful commit.",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Run one full cycle through all configured agents, then exit.",
    )
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    config_path = (repo_root / args.config).resolve()
    progress_path = (repo_root / args.progress).resolve()

    if not config_path.exists():
        print(f"Config not found: {config_path}", file=sys.stderr)
        return 1
    if not progress_path.exists():
        print(f"Progress file not found: {progress_path}", file=sys.stderr)
        return 1

    agents = read_agents(config_path)
    if not agents:
        print(f"No valid agents found in {config_path}", file=sys.stderr)
        return 1

    cycle = 0
    while True:
        cycle += 1
        print(f"\n##### Supervisor cycle {cycle} #####")
        for agent in agents:
            before = read_completed_task_ids(progress_path)
            run_agent(agent, repo_root)
            after = read_completed_task_ids(progress_path)
            newly_completed = sorted(after - before)

            if not newly_completed:
                print(f"No newly completed tasks for {agent.name}.")
                time.sleep(args.sleep_seconds)
                continue

            if not working_tree_has_changes(repo_root):
                print(
                    f"Tasks changed in progress file but no git changes detected for {agent.name}.",
                    file=sys.stderr,
                )
                time.sleep(args.sleep_seconds)
                continue

            msg = format_commit_message(newly_completed)
            if not msg:
                print(f"Unable to derive commit message for {agent.name}.", file=sys.stderr)
                time.sleep(args.sleep_seconds)
                continue

            print(f"Committing changes with message: {msg}")
            try:
                committed = commit_progress(repo_root, msg, args.push)
                if committed:
                    print(f"Committed {agent.name}: {msg}")
                else:
                    print(f"No staged changes for {agent.name}; commit skipped.")
            except subprocess.CalledProcessError as exc:
                command_text = " ".join(shlex.quote(p) for p in exc.cmd)
                print(f"Commit step failed: {command_text}", file=sys.stderr)
                if exc.stdout:
                    print(exc.stdout, file=sys.stderr)
                if exc.stderr:
                    print(exc.stderr, file=sys.stderr)

            time.sleep(args.sleep_seconds)

        if args.once:
            break

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
