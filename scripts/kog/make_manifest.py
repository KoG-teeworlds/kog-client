#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import sys
import time

MAX_FILES = 4096
MAX_UPLOADS = 900
MAX_VALIDITY_DAYS = {"stable": 90, "canary": 30, "dev": 14}
VERSION_FORMS = {
    "stable": r"[0-9]+\.[0-9]+\.[0-9]+",
    "canary": r"[0-9]+\.[0-9]+\.[0-9]+-canary\.[0-9]+",
    "dev": r"[0-9]+\.[0-9]+\.[0-9]+-dev\.[0-9]+",
}
SERVER_NAMES = ("ddnet-server", "kog-server", "ddnet-server.exe")
FORBIDDEN = (".exe", ".dll", ".so", ".dylib", ".sh", ".bat", ".cmd", ".ps1", ".app", ".jar", ".py")


def allowed_path(path):
    if not path.startswith("data/") or len(path) > 200:
        return False
    parts = path.split("/")
    for part in parts:
        if not part or not re.fullmatch(r"[A-Za-z0-9._ +-]+", part) or part.startswith("."):
            return False
    return not path.lower().endswith(FORBIDDEN)


def is_server(path):
    name = path.replace("\\", "/").rsplit("/", 1)[-1].lower()
    return name in SERVER_NAMES or name.startswith("ddnet-server")


def check_version(channel, version):
    if channel not in VERSION_FORMS or not re.fullmatch(VERSION_FORMS[channel], version):
        sys.exit("version %s does not have the form of the %s channel" % (version, channel))


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree_hashes(root):
    result, skipped = {}, []
    data_root = os.path.join(root, "data")
    for folder, _, names in os.walk(data_root):
        for name in names:
            full = os.path.join(folder, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            if allowed_path(rel):
                result[rel] = full
            else:
                skipped.append(rel)
    return result, skipped


def asset_url(repo, tag, name):
    return "https://github.com/%s/releases/download/%s/%s" % (repo, tag, name)


def write_json(path, value):
    with open(path, "w") as f:
        json.dump(value, f, indent=2)
        f.write("\n")


def cmd_draft(args):
    check_version(args.channel, args.version)
    tree, skipped = tree_hashes(args.data)
    servers = [rel for rel in tree if is_server(rel)]
    if servers:
        sys.exit("a server file is in the release data: " + ", ".join(servers))
    for rel in skipped:
        print("skipped (not updatable): " + rel, file=sys.stderr)
    hashes = {rel: sha256_file(full) for rel, full in tree.items()}
    if args.init_base:
        write_json(args.init_base, hashes)
        base = hashes
    else:
        base = json.load(open(args.base))
    known = {}
    if args.previous:
        previous = json.load(open(args.previous))
        for entry in previous.get("files", []):
            known[entry["sha256"]] = entry["url"]
    files, uploads = [], []
    for rel in sorted(hashes):
        if base.get(rel) == hashes[rel]:
            continue
        sha = hashes[rel]
        url = known.get(sha) or asset_url(args.repo, args.tag, sha)
        if sha not in known:
            uploads.append({"asset": sha, "source": tree[rel]})
        files.append({"path": rel, "sha256": sha, "size": os.path.getsize(tree[rel]), "url": url})
    remove = sorted(rel for rel in base if rel not in hashes)
    platforms = {}
    for spec in args.client:
        platform, _, path = spec.partition("=")
        name = "client-%s-%s%s" % (platform, args.version, ".exe" if platform.startswith("win") else "")
        if is_server(path):
            sys.exit("a server executable cannot be the client: " + path)
        platforms[platform] = {"client": {"sha256": sha256_file(path), "size": os.path.getsize(path), "url": asset_url(args.repo, args.tag, name)}}
        uploads.append({"asset": name, "source": path})
    if len(files) > MAX_FILES or len(uploads) > MAX_UPLOADS:
        sys.exit("too many changed files for one release (%d files, %d uploads): publish a new base" % (len(files), len(uploads)))
    manifest = {
        "format": 2,
        "channel": args.channel,
        "sequence": 0,
        "issued": 0,
        "expires": 0,
        "version": args.version,
        "files": files,
        "platforms": platforms,
        "remove": remove,
    }
    if args.rollout_percent is not None:
        manifest["rollout_percent"] = args.rollout_percent
    if args.min_version:
        manifest["min_version"] = args.min_version
    if args.notes_url:
        manifest["notes_url"] = args.notes_url
    if args.change:
        if len(args.change) > 3 or any(len(c) > 120 or len(c) == 0 for c in args.change):
            sys.exit("at most 3 changes of at most 120 characters")
        manifest["changes"] = args.change
    write_json(args.out, manifest)
    write_json(args.uploads, uploads)
    print("draft: %d changed files, %d removals, %d uploads" % (len(files), len(remove), len(uploads)))


def cmd_finalize(args):
    manifest = json.load(open(args.draft))
    now = args.now if args.now is not None else int(time.time())
    limit = MAX_VALIDITY_DAYS.get(manifest.get("channel"))
    if limit is None:
        sys.exit("the draft has no known channel")
    check_version(manifest["channel"], manifest["version"])
    if not 1 <= args.days <= limit:
        sys.exit("--days must be between 1 and %d for the %s channel" % (limit, manifest["channel"]))
    if args.sequence < 1:
        sys.exit("--sequence must be at least 1")
    manifest["sequence"] = args.sequence
    manifest["issued"] = now
    manifest["expires"] = now + args.days * 86400
    write_json(args.out, manifest)


def version_key(version):
    match = re.fullmatch(r"([0-9]+)\.([0-9]+)\.([0-9]+)(?:-[a-z]+\.([0-9]+))?", version)
    if not match:
        sys.exit("not a version: " + version)
    major, minor, patch, build = match.groups()
    return (int(major), int(minor), int(patch), int(build) if build is not None else float("inf"))


def cmd_newer(args):
    manifest = json.load(open(args.manifest))
    check_version(manifest["channel"], manifest["version"])
    if not os.path.exists(args.previous):
        return
    previous = json.load(open(args.previous))
    if previous.get("channel") != manifest["channel"]:
        sys.exit("the published manifest is of another channel")
    if version_key(manifest["version"]) <= version_key(previous["version"]):
        sys.exit("version %s is not newer than the published %s" % (manifest["version"], previous["version"]))


def cmd_halt(args):
    manifest = json.load(open(args.previous))
    now = args.now if args.now is not None else int(time.time())
    limit = MAX_VALIDITY_DAYS[manifest["channel"]]
    if not 1 <= args.days <= limit:
        sys.exit("--days must be between 1 and %d" % limit)
    manifest["halted"] = True
    manifest["sequence"] = manifest["sequence"] + 1
    manifest["issued"] = now
    manifest["expires"] = now + args.days * 86400
    write_json(args.out, manifest)


def cmd_check(args):
    manifest = json.load(open(args.manifest))
    entries = [e for e in manifest.get("files", [])]
    for platform in manifest.get("platforms", {}).values():
        if "client" in platform:
            entries.append(platform["client"])
    bad = 0
    for entry in entries:
        path = os.path.join(args.assets, entry["url"].rsplit("/", 1)[1])
        if not os.path.exists(path):
            continue
        if sha256_file(path) != entry["sha256"] or os.path.getsize(path) != entry["size"]:
            print("MISMATCH: " + path, file=sys.stderr)
            bad += 1
    sys.exit(1 if bad else 0)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    draft = sub.add_parser("draft")
    draft.add_argument("--version", required=True)
    draft.add_argument("--channel", default="stable")
    draft.add_argument("--repo", default="KoG-teeworlds/kog-client")
    draft.add_argument("--tag", required=True)
    draft.add_argument("--data", required=True)
    draft.add_argument("--base")
    draft.add_argument("--init-base")
    draft.add_argument("--previous")
    draft.add_argument("--rollout-percent", type=int, choices=range(0, 101), metavar="0-100")
    draft.add_argument("--min-version")
    draft.add_argument("--notes-url")
    draft.add_argument("--change", action="append", default=[])
    draft.add_argument("--client", action="append", default=[])
    draft.add_argument("--out", required=True)
    draft.add_argument("--uploads", required=True)
    final = sub.add_parser("finalize")
    final.add_argument("--draft", required=True)
    final.add_argument("--sequence", type=int, required=True)
    final.add_argument("--days", type=int, default=30)
    final.add_argument("--now", type=int)
    final.add_argument("--out", required=True)
    newer = sub.add_parser("newer")
    newer.add_argument("--manifest", required=True)
    newer.add_argument("--previous", required=True)
    halt = sub.add_parser("halt")
    halt.add_argument("--previous", required=True)
    halt.add_argument("--days", type=int, default=7)
    halt.add_argument("--now", type=int)
    halt.add_argument("--out", required=True)
    check = sub.add_parser("check")
    check.add_argument("--manifest", required=True)
    check.add_argument("--assets", required=True)
    args = parser.parse_args()
    if args.command == "draft" and not (args.base or args.init_base):
        parser.error("draft needs --base or --init-base")
    {"draft": cmd_draft, "finalize": cmd_finalize, "halt": cmd_halt, "newer": cmd_newer, "check": cmd_check}[args.command](args)


main()
