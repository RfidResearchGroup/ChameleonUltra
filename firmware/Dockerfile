FROM ubuntu:22.04

ARG ARM_TOOLCHAIN_VERSION=12.2.rel1
ARG NRF_CLT_URL=https://nsscprodmedia.blob.core.windows.net/prod/software-and-other-downloads/desktop-software/nrf-command-line-tools/sw/versions-10-x-x/10-22-1/nrf-command-line-tools_10.22.1_amd64.deb

RUN set -xe; \
  DEBIAN_FRONTEND=noninteractive; \
  apt update -q; \
  apt install -qy --no-install-recommends ca-certificates curl xz-utils make git; \
  git config --global --add safe.directory /workdir; \
  curl -sLo /usr/bin/nrfutil https://developer.nordicsemi.com/.pc-tools/nrfutil/x64-linux/nrfutil; \
  curl -sLo /tmp/nrf-clt.deb $NRF_CLT_URL; \
  apt install -qfy /tmp/nrf-clt.deb; \
  rm /tmp/nrf-clt.deb; \
  chmod 755 /usr/bin/nrfutil; \
  nrfutil install device nrf5sdk-tools; \
  curl -sL https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu/$ARM_TOOLCHAIN_VERSION/binrel/arm-gnu-toolchain-$ARM_TOOLCHAIN_VERSION-x86_64-arm-none-eabi.tar.xz | tar xJ -C /usr/local; \
  apt clean -q; \
  rm -rf /var/lib/apt/lists/*


ENV GNU_INSTALL_ROOT=/usr/local/arm-gnu-toolchain-$ARM_TOOLCHAIN_VERSION-x86_64-arm-none-eabi/bin/
ENV GNU_VERSION=$ARM_TOOLCHAIN_VERSION

WORKDIR /workdir
