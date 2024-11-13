FROM deps-build AS builder

ARG LIB_REDIS_PLUS_PLUS_VERSION=1.2.3

# Apply patch and re-install Redis plus plus
# RUN cd /tmp/redis-plus-plus\
# && sed -i '/Transaction transaction/i\\    ShardsPool* get_shards_pool(){\n        return &_pool;\n    }\n' \
#    src/sw/redis++/redis_cluster.h \
# && cmake -DREDIS_PLUS_PLUS_USE_TLS=ON . \
# && make -j$(nproc) \
# && make install

COPY ./ /social-network-microservices
RUN cd /social-network-microservices \
    && mkdir -p build \
    && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Debug .. \
    && make -j$(nproc) \
    && make install 


FROM ubuntu:jammy

# Copy compiled C++ binaries and dependencies
COPY --from=builder /usr/local/bin/* /usr/local/bin/
COPY --from=builder /usr/local/lib/* /usr/local/lib/

COPY ./docker/pin-script.sh /social-network-microservices

# Install system dependencies
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        openssl \
        ca-certificates \
        libsasl2-2 \
        libmemcached11 \
        libmemcachedutil2 \
		wget \
		python3-dev \
		libopenmpi-dev \
		openmpi-bin \
		git \
	libicu-dev \
	curl \
	make \
	g++ \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    # Install pin
    && mkdir /social-network-microservices \
    && cd /social-network-microservices \
    && mkdir pin \
    && curl https://software.intel.com/sites/landingpage/pintool/downloads/pin-external-3.31-98869-gfa6f126a8-gcc-linux.tar.gz | tar xvz -C pin --strip-components 1 \
    && cd pin/source/tools/ManualExamples \
    && make all TARGET=intel64 \
    # Install SST
	&& cd /social-network-microservices \
	&& wget https://github.com/sstsimulator/sst-core/releases/download/v14.1.0_Final/sstcore-14.1.0.tar.gz \
	&& tar -xvf sstcore-14.1.0.tar.gz \
	&& rm sstcore-14.1.0.tar.gz \
	&& cd sst-core \
	&& ./configure \
	&& make install \
	&& make \
	&& cd .. \
	&& wget https://github.com/sstsimulator/sst-elements/releases/download/v14.1.0_Final/sstelements-14.1.0.tar.gz \	
	&& tar -xvf sstelements-14.1.0.tar.gz \
	&& rm sstelements-14.1.0.tar.gz \
	&& mkdir /root/.sst \
	&& touch /root/.sst/sstsimulator.conf \
	&& cd sst-elements \
	&& ./configure \
	&& make install  \
	# Apply SST fix
	&& git clone https://github.com/ansaschmulbach/sst-elements.git ansa-sst \
	&& cp /social-network-microservices/ansa-sst/src/sst/elements/prospero/tracetool/Makefile /social-network-microservices/sst-elements/src/sst/elements/prospero/tracetool/ \
	&& cp /social-network-microservices/ansa-sst/src/sst/elements/prospero/tracetool/sstmemtrace.cc /social-network-microservices/sst-elements/src/sst/elements/prospero/tracetool/ \
	&& cp /social-network-microservices/ansa-sst/src/sst/elements/prospero/tests/array/trace-common.py /social-network-microservices/sst-elements/src/sst/elements/prospero/tests/array/trace-common.py \
	&& cd /social-network-microservices/sst-elements/src/sst/elements/prospero/tracetool/ \
	&& export INTEL_PIN_DIRECTORY=/social-network-microservices/pin \
	&& make

WORKDIR /social-network-microservices
