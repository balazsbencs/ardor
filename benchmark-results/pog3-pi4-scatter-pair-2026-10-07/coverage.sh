#!/bin/sh
set -eu
exec ./coverage-candidate --freeze-matched forward ./shared.wisdom
