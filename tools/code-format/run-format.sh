#!/bin/bash

# Files listed in .clang-format-ignore are skipped by clang-format itself.
cd /repository
find . -path ./third_party -prune -o -path ./.git -prune -o \
  \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hh' \) -print | while read f ; do
  clang-format -i "$f"
  sed "s/ *$//" -i "$f"
done
