FROM registry.suse.com/suse/sle15:15.6

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

WORKDIR /tmp

USER root

#hadolint ignore=DL3041
RUN  zypper refresh && \
     zypper --non-interactive install -y \
      gawk \
      wget \
      cmake \
      gcc \
      gcc-c++ \
      ninja \
      sudo \
      wget \
      awk \
      libprocps8 \
      libsystemd0 \
      procps \
      which \
      python311

RUN update-alternatives --install /usr/local/bin/python python /usr/bin/python3.11 10

RUN zypper addrepo https://yum.repos.intel.com/oneapi oneAPI && \
  rpm --import https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB && \
  zypper addrepo -f -r https://repositories.intel.com/gpu/sles/15sp6/unified/intel-gpu-15sp6.repo && \
  rpm --import https://repositories.intel.com/gpu/intel-graphics.key

RUN zypper --non-interactive install -y \
  intel-basekit-2024.2.1-98 \
  intel-level-zero-gpu \
  level-zero \
  intel-gsc \
  intel-opencl \
  intel-ocloc \
  intel-media-driver \
  libigfxcmrt7 \
  libvpl2 \
  libvpl-tools \
  libmfxgen1 \
  libigdfcl-devel \
  intel-igc-cm \
  libigfxcmrt-devel \
  level-zero-devel