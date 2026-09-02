#!/bin/bash
# cache volume starts root-owned; cvmfs2 drops to the cvmfs user
mkdir -p /var/lib/cvmfs && chown cvmfs:cvmfs /var/lib/cvmfs
for repo in sft.cern.ch sft-nightlies.cern.ch; do
  mkdir -p /cvmfs/$repo
  mountpoint -q /cvmfs/$repo || mount -t cvmfs $repo /cvmfs/$repo || echo "WARN: failed to mount $repo"
done
exec "$@"
