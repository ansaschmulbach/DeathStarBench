FROM yg397/thrift-microservice-deps:xenial AS builder

ARG LIB_REDIS_PLUS_PLUS_VERSION=1.2.3

RUN apt-get -qq update \
    && apt-get -qq install -y --no-install-recommends \
       sudo \
       binutils \
       wget \
       git \
       g++-multilib \
       libgmp-dev \
       libmpfr-dev \
       libmpc-dev \
       nasm \
       dh-autoreconf \
       libffi-dev \
       libssl-dev \
       pkg-config \
       subversion \
       zlib1g-dev \
       libbz2-dev \
       libsqlite3-dev \
       libreadline-dev \
       xz-utils \
       curl \
       libncurses5-dev \
       libncursesw5-dev \
       liblzma-dev \
       ca-certificates \
       autoconf-archive \
       && rm -rf /var/lib/apt/lists/*

RUN wget -q --no-check-certificate http://mirrors.concertpass.com/gcc/releases/gcc-10.2.0/gcc-10.2.0.tar.xz \
    && tar Jxf gcc-10.2.0.tar.xz

RUN cd gcc-10.2.0 \
    && ./configure --build=x86_64-linux-gnu --disable-multilib --enable-languages=c,c++ --prefix=/opt/gcc \
    && make -j2 \
    && make install-strip

# Remove existing symbolic link if it exists and create a new one
RUN if [ -L /usr/bin/gcc ]; then rm /usr/bin/gcc; fi \
    && if [ -L /usr/bin/g++ ]; then rm /usr/bin/g++; fi \
    && ln -s /opt/gcc/bin/gcc /usr/bin/gcc \
    && ln -s /opt/gcc/bin/g++ /usr/bin/g++

RUN apt-get update && \
    apt-get install -y wget build-essential

RUN wget https://github.com/Kitware/CMake/releases/download/v3.14.7/cmake-3.14.7-Linux-x86_64.sh -O /tmp/cmake-install.sh && \
    chmod +x /tmp/cmake-install.sh && \
    mkdir /opt/cmake && \
    /tmp/cmake-install.sh --skip-license --prefix=/opt/cmake && \
    ln -s /opt/cmake/bin/* /usr/local/bin && \
    rm /tmp/cmake-install.sh

# Clone the repository
RUN git clone https://github.com/jmuehlig/perf-cpp.git

# Switch to the repository folder
WORKDIR perf-cpp

# Optional: Switch to the latest stable version
RUN git checkout v0.10.0

# Build the library (in build/)
# Note: -DBUILD_EXAMPLES=1 is optional and only needed if you want to build the examples
RUN mkdir build && cd build && cmake .. -DBUILD_LIB_SHARED=1 -DCMAKE_INSTALL_LIBRARY_DIR=/usr/local/lib && make && make install

# Apply patch and re-install Redis plus plus
RUN cd /tmp/redis-plus-plus\
&& sed -i '/Transaction transaction/i\\    ShardsPool* get_shards_pool(){\n        return &_pool;\n    }\n' \
   src/sw/redis++/redis_cluster.h \
&& cmake -DREDIS_PLUS_PLUS_USE_TLS=ON -DREDIS_PLUS_PLUS_CXX_STANDARD="17" . \
&& make -j$(nproc) \
&& make install

# Clone the rabbitmq-c repository
RUN git clone  --branch v0.9.0 https://github.com/alanxz/rabbitmq-c.git /tmp/rabbitmq-c

# Create a build directory and compile rabbitmq-c
RUN mkdir -p /tmp/rabbitmq-c/build && cd /tmp/rabbitmq-c/build \
    && cmake -DCMAKE_CXX_STANDARD=17 .. \
    && make \
    && make install

# Download and extract Boost source
RUN wget https://archives.boost.io/release/1.58.0/source/boost_1_58_0.tar.gz -O /tmp/boost_1_58_0.tar.gz \
    && mkdir -p /tmp/boost \
    && tar --strip-components=1 -xzf /tmp/boost_1_58_0.tar.gz -C /tmp/boost

# Build and install Boost
RUN cd /tmp/boost \
    && ./bootstrap.sh \
    && ./b2 install --prefix=/usr/local

COPY ./ /social-network-microservices
RUN cd /social-network-microservices \
    && mkdir -p build \
    && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Debug .. \
    && make -j$(nproc) \
    && make install

FROM ubuntu:16.04

# Copy compiled C++ binaries and dependencies
COPY --from=builder /usr/local/bin/* /usr/local/bin/
COPY --from=builder /usr/local/lib/* /usr/local/lib/
COPY --from=builder /usr/local/share/* /usr/local/share/
COPY --from=builder /usr/local/share/* /usr/local/share/

# Copy runtime libraries
COPY --from=builder /opt/gcc/lib64/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/
COPY --from=builder /opt/gcc/lib64/libgcc_s.so.1 /usr/lib/x86_64-linux-gnu/

# Set LD_LIBRARY_PATH
ENV LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH

COPY --from=builder /usr/lib/gcc /usr/lib/gcc
COPY --from=builder /opt/gcc /opt/gcc

RUN if [ -L /usr/bin/gcc ]; then rm /usr/bin/gcc; fi \
    && if [ -L /usr/bin/g++ ]; then rm /usr/bin/g++; fi \
    && ln -s /opt/gcc/bin/gcc /usr/bin/gcc \
    && ln -s /opt/gcc/bin/g++ /usr/bin/g++


# Install system dependencies
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        openssl \
        ca-certificates \
        libsasl2-2 \
        libmemcached11 \
        libmemcachedutil2 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /social-network-microservices
