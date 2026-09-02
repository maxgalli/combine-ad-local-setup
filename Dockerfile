FROM almalinux:9

RUN dnf install -y https://ecsft.cern.ch/dist/cvmfs/cvmfs-release/cvmfs-release-latest.noarch.rpm && \
    dnf install -y cvmfs cvmfs-config-default fuse fuse3 \
                   make binutils which tar gzip patch git diffutils findutils \
                   glibc-devel libstdc++-devel kernel-headers \
                   libSM libICE libX11 libXext libXpm libXft mesa-libGL && \
    dnf clean all

RUN printf '%s\n' \
    'CVMFS_CLIENT_PROFILE=single' \
    'CVMFS_REPOSITORIES=sft.cern.ch,sft-nightlies.cern.ch' \
    'CVMFS_HTTP_PROXY=DIRECT' \
    'CVMFS_SERVER_URL="http://s1cern-cvmfs.openhtc.io/cvmfs/@fqrn@;http://cvmfs-stratum-one.cern.ch/cvmfs/@fqrn@"' \
    'CVMFS_QUOTA_LIMIT=20000' \
    'CVMFS_CACHE_BASE=/var/lib/cvmfs' \
    > /etc/cvmfs/default.local

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
