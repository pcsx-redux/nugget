#!/bin/sh

cd $(dirname $0)/../..

docker build -t nugget-code-format tools/code-format
docker run --rm=true -v "$(pwd)":/repository --user "$(id -u):$(id -g)" -t nugget-code-format
