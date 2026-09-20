#!/usr/bin/env sh
# Report CI status from the GitHub API, per job.
#
# Written after a long run of reporting "CI green" that was not true: the
# rendered Actions page is easy to misread, and a workflow run's own
# conclusion is the only thing worth trusting. Takes a ref (default: the
# current HEAD).
set -eu

REPO=${REPO:-galoryber/BackingTrackLive}
REF=${1:-$(git rev-parse HEAD)}

printf 'repo %s\nref  %s\n\n' "$REPO" "$REF"

python3 - "$REPO" "$REF" <<'PY'
import json, sys, urllib.request
repo, ref = sys.argv[1], sys.argv[2]

def api(url):
    req = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json", "User-Agent": "ci-status"})
    return json.load(urllib.request.urlopen(req, timeout=30))

try:
    d = api(f"https://api.github.com/repos/{repo}/commits/{ref}/check-runs")
except Exception as e:
    print("could not reach the API:", e)
    sys.exit(2)

runs = d.get("check_runs", [])
if not runs:
    print("no check runs yet for this ref")
    sys.exit(3)

# Newest result per job name.
latest = {}
for r in runs:
    latest.setdefault(r["name"], r)

bad = 0
for name in sorted(latest):
    r = latest[name]
    state = r["conclusion"] or r["status"]
    if state not in ("success", "neutral", "skipped"):
        bad += 1
    print(f"  {state:<12} {name}")

print()
print("FAILING" if bad else "all green", f"({len(latest)} job(s))")
sys.exit(1 if bad else 0)
PY
