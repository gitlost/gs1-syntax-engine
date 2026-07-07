#! /bin/bash

set -euo pipefail

IMAGE=gs1encoder-swift-doc-build

cd $(dirname $0)
docker build . -t "$IMAGE"

cd ../../..

# Shadow src/swift/.build with a tmpfs: the container's SwiftPM state and the
# host's must never mix (incompatible toolchains, and formerly root-owned
# artifacts). The mount point is pre-created so that docker does not create
# it root-owned on the host.
mkdir -p src/swift/.build

exec docker run --rm -u `id -u`:`id -g` -v `pwd`:/srv \
	--tmpfs /srv/src/swift/.build:rw,mode=1777 \
	-w /srv/src/swift "$IMAGE"
