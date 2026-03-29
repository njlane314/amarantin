ARG ROOT_IMAGE=rootproject/root:6.28.12-ubuntu22.04
FROM ${ROOT_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        build-essential \
        ca-certificates \
        git \
        libsqlite3-dev \
        make \
        nlohmann-json3-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

ENV ROOT_CONFIG=root-config

CMD ["bash"]
