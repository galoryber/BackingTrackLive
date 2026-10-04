#!/usr/bin/env sh
# Report CI status from the GitHub API, per job.
#
# Written after a long run of reporting "CI green" that was not true: the
# rendered Actions page is easy to misread, and a workflow run's own
# conclusion is the only thing worth trusting. Takes a ref (default: the
# current HEAD).
#
# Exit codes: 0 green, 1 a job failed, 2 still running. Those are distinct on
# purpose - treating "in progress" as a failure is the same class of mistake
# as treating a failure as success, just in the other direction.
set -eu

REPO=${REPO:-galoryber/BackingTrackLive}
REF=${1:-$(git rev-parse HEAD)}

# Authenticate if a token is available. Unauthenticated, the API allows sixty
# requests an hour, which a couple of CI runs' worth of polling exhausts; with
# a token it is five thousand. Entirely optional - everything here works
# without one, just less often.
#
# Only a path is ever recorded here. The token itself lives outside the
# repository and must stay there.
if [ -z "${GH_TOKEN:-}" ] && [ -z "${GITHUB_TOKEN:-}" ]; then
  for f in "$HOME/.config/backtracklive/gh-token" "$HOME/githubPAT.txt"; do
    if [ -r "$f" ]; then
      GH_TOKEN=$(tr -d ' \t\r\n' < "$f")
      export GH_TOKEN
      break
    fi
  done
fi

printf 'repo %s\nref  %s\n\n' "$REPO" "$REF"

python3 - "$REPO" "$REF" <<'PY'
import json, os, sys, urllib.request
repo, ref = sys.argv[1], sys.argv[2]

def api(url):
    h = {"Accept": "application/vnd.github+json", "User-Agent": "ci-status"}
    tok = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if tok:
        h["Authorization"] = "Bearer " + tok
    req = urllib.request.Request(url, headers=h)
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
pending = 0
for name in sorted(latest):
    r = latest[name]
    done = r["status"] == "completed"
    state = r["conclusion"] if done else r["status"]
    if not done:
        pending += 1
    elif state not in ("success", "neutral", "skipped"):
        bad += 1
    print(f"  {state:<12} {name}")

print()
# "Still running" is not "failing". Conflating them is how a green run gets
# reported as a red one, which is the same class of mistake as the reverse.
if bad:
    print(f"FAILING ({bad} of {len(latest)} job(s))")
    sys.exit(1)
if pending:
    print(f"PENDING ({pending} of {len(latest)} job(s) still running)")
    sys.exit(2)
print(f"all green ({len(latest)} job(s))")
sys.exit(0)
PY
