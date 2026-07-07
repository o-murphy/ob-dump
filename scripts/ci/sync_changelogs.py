#!/usr/bin/env python3
"""Generates <package>/CHANGELOG.md from the repo root's single
hand-authored CHANGELOG.md — the only source of truth for changelog
content. Not committed anywhere: pub.dev requires a package to ship its
own CHANGELOG.md, so release.yml runs this ephemerally, right before
publish-dart/publish-flutter, in that ephemeral checkout only (same
pattern already used for flutter/pubspec.yaml's publish-time
dependency-version swap — never written back to the repo).

dart/.gitignore and flutter/.gitignore deliberately do NOT ignore
CHANGELOG.md, even though it's generated: `pub publish` decides package
contents by git tracking status, and explicitly excludes gitignored files
even if force-added to the index (confirmed empirically — `git add -f`
still gets excluded, with a "checked in while gitignored" warning) — an
untracked-but-not-ignored file is what actually works, no commit needed.

Root file format: version headings ("## [X]" or "## [X] - DATE"), each
containing one "### <pkg>/" subsection per package that changed in that
version (pkg in dart/flutter/py/js), with #### sub-headings (Changed/Added/
Removed/Fixed) one level deeper than each package's own file uses (since
the package file has no "### <pkg>/" wrapper of its own).

Usage: scripts/ci/sync_changelogs.py [pkg ...]
  No args: generates dart/CHANGELOG.md, flutter/CHANGELOG.md, py/CHANGELOG.md,
  and js/CHANGELOG.md. With args, only the named package(s), e.g.:
    scripts/ci/sync_changelogs.py dart
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
PACKAGES = ("dart", "flutter", "py", "js")

VERSION_HEADING_RE = re.compile(r"^## \[(?P<version>[^\]]+)\](?P<rest>.*)$")
PKG_HEADING_RE = re.compile(r"^### (?P<pkg>\w+)/\s*$")
LINK_REF_RE = re.compile(r"^\[.+\]:\s")

PER_PACKAGE_INTRO = """\
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Generated from the repo root's `CHANGELOG.md` at publish time — see that
file if you're reading this in the published package and want the full
picture across the other packages in this repo too.
"""

PY_VERSION_NOTE = (
    "Version numbers are derived from git tags via `setuptools_scm`, not"
    " hand-bumped in this file."
)

# Versions with no corresponding git tag — published by hand outside the
# normal release.yml/tag flow, so a GitHub compare/tag link would 404.
NO_TAG_VERSIONS = {"0.1.0-alpha.0"}
PUB_DEV_VERSION_URLS = {
    (
        "dart",
        "0.1.0-alpha.0",
    ): "https://pub.dev/packages/ob_dump_reader/versions/0.1.0-alpha.0",
    (
        "flutter",
        "0.1.0-alpha.0",
    ): "https://pub.dev/packages/ob_dump_reader_flutter/versions/0.1.0-alpha.0",
}


def parse_root(text: str):
    """Returns a list of (heading_line, {pkg: [content_lines]}) in source order."""
    lines = text.splitlines()
    versions = []
    heading = None
    sections: dict[str, list[str]] | None = None
    i = 0
    while i < len(lines):
        line = lines[i]
        if VERSION_HEADING_RE.match(line):
            if heading is not None:
                versions.append((heading, sections))
            heading = line
            sections = {}
            i += 1
            continue
        if heading is not None:
            m = PKG_HEADING_RE.match(line)
            if m:
                pkg = m.group("pkg")
                i += 1
                content = []
                while (
                    i < len(lines)
                    and not VERSION_HEADING_RE.match(lines[i])
                    and not PKG_HEADING_RE.match(lines[i])
                    and not LINK_REF_RE.match(lines[i])
                ):
                    content.append(lines[i])
                    i += 1
                while content and not content[-1].strip():
                    content.pop()
                while content and not content[0].strip():
                    content.pop(0)
                sections[pkg] = content
                continue
        i += 1
    if heading is not None:
        versions.append((heading, sections))
    return versions


def demote_headings(lines: list[str]) -> list[str]:
    """One "### <pkg>/" wrapper level is dropped per package, so its
    "#### Foo" sub-headings become "### Foo" in the per-package file."""
    return [line[1:] if line.startswith("#### ") else line for line in lines]


def build_footer(pkg: str, included: list[tuple[str, list[str]]]) -> list[str]:
    # `included` is in source order (newest first), matching the root file.
    lines = []
    for idx, (heading, _content) in enumerate(included):
        version = VERSION_HEADING_RE.match(heading).group("version")
        next_older = None
        if idx + 1 < len(included):
            next_older = VERSION_HEADING_RE.match(included[idx + 1][0]).group("version")

        if version == "Unreleased":
            if next_older and next_older not in NO_TAG_VERSIONS:
                lines.append(
                    f"[Unreleased]: https://github.com/o-murphy/ob-dump/compare/v{next_older}...HEAD"
                )
            else:
                lines.append(
                    f"[Unreleased]: https://github.com/o-murphy/ob-dump/commits/main/{pkg}"
                )
        elif version in NO_TAG_VERSIONS:
            url = PUB_DEV_VERSION_URLS.get((pkg, version))
            if url:
                lines.append(f"[{version}]: {url}")
        elif next_older and next_older not in NO_TAG_VERSIONS:
            lines.append(
                f"[{version}]: https://github.com/o-murphy/ob-dump/compare/v{next_older}...v{version}"
            )
        else:
            lines.append(
                f"[{version}]: https://github.com/o-murphy/ob-dump/releases/tag/v{version}"
            )
    return lines


def render_package_changelog(pkg: str, versions: list[tuple[str, dict]]) -> str:
    included = [(h, s[pkg]) for h, s in versions if s.get(pkg)]

    body_parts = [PER_PACKAGE_INTRO]
    if pkg == "py":
        body_parts.append(PY_VERSION_NOTE + "\n")

    for heading, content in included:
        body_parts.append(heading)
        body_parts.append("")
        body_parts.extend(demote_headings(content))
        body_parts.append("")

    footer = build_footer(pkg, included)

    text = "\n".join(body_parts).rstrip("\n") + "\n\n" + "\n".join(footer) + "\n"
    return re.sub(
        r"\n{3,}", "\n\n", text
    )  # collapse the intro's trailing blank-line joins


def main() -> int:
    requested = sys.argv[1:] or list(PACKAGES)
    unknown = set(requested) - set(PACKAGES)
    if unknown:
        print(
            f"Unknown package(s): {', '.join(sorted(unknown))} (expected: {', '.join(PACKAGES)})",
            file=sys.stderr,
        )
        return 1

    versions = parse_root((REPO_ROOT / "CHANGELOG.md").read_text())
    for pkg in requested:
        dest = REPO_ROOT / pkg / "CHANGELOG.md"
        dest.write_text(render_package_changelog(pkg, versions))
        print(f"Generated {dest.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
