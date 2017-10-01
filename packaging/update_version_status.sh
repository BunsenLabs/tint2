#!/bin/bash

set -e
set -x

[ "${FLOCKER}" != "$0" ] && exec env FLOCKER="$0" flock -en "$0" "$0" "$@" || :

exec > ~/tint2.runner-version.log
exec 2>&1

cd ~/tint2.wiki
git reset --hard
git pull


~/tint2/packaging/version_status.py > packaging.tmp.md
cat packaging.tmp.md > packaging.md
rm packaging.tmp.md

git commit -am 'Update packaging info'
git push origin master
