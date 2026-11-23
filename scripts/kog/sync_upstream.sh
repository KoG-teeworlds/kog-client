#!/usr/bin/env bash
set -euo pipefail

[ $# -eq 1 ] || { echo "usage: $0 UPSTREAM_REF   (a TClient tag or branch)" >&2; exit 2; }
ref=$1
git remote get-url upstream >/dev/null 2>&1 || git remote add upstream https://github.com/TaterClient/TClient.git
git fetch upstream --tags

git checkout master
git merge --ff-only upstream/master

git checkout -b "sync/$ref" main
target="upstream/$ref"
git rev-parse -q --verify "refs/tags/$ref" >/dev/null && target="$ref"
if git merge --no-commit --no-ff "$target"; then
  echo "merged without conflicts, review with: git diff --cached"
else
  echo "conflicts:"
  git diff --name-only --diff-filter=U
fi
echo "then update KOG_BASED_ON_TCLIENT and KOG_BASED_ON_DDNET in src/game/kog_version.h and build"
echo "master is the mirror: push it with 'git push origin master'"
