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


FROM ubuntu:jammy AS installs

# Copy compiled C++ binaries and dependencies
COPY --from=builder /usr/local/bin/* /usr/local/bin/
COPY --from=builder /usr/local/lib/* /usr/local/lib/

COPY src/pin-script.sh /social-network-microservices/

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
    && apt-get clean && rm -rf /var/lib/apt/lists/* 

# Install pin
WORKDIR /social-network-microservices
RUN mkdir pin \
    && curl https://software.intel.com/sites/landingpage/pintool/downloads/pin-external-3.31-98869-gfa6f126a8-gcc-linux.tar.gz | tar xvz -C pin --strip-components 1 \
    && cd pin/source/tools/ManualExamples \
    && make all TARGET=intel64 
	

# Install SST
WORKDIR /social-network-microservices 
RUN wget https://github.com/sstsimulator/sst-core/releases/download/v14.1.0_Final/sstcore-14.1.0.tar.gz \
	&& tar -xvf sstcore-14.1.0.tar.gz \
	&& rm sstcore-14.1.0.tar.gz \
	&& cd sst-core \
	&& ./configure \
	&& make install \
	&& make 

WORKDIR /social-network-microservices 
RUN wget https://github.com/sstsimulator/sst-elements/releases/download/v14.1.0_Final/sstelements-14.1.0.tar.gz \	
	&& tar -xvf sstelements-14.1.0.tar.gz \
	&& rm sstelements-14.1.0.tar.gz \
	&& mkdir /root/.sst \
	&& touch /root/.sst/sstsimulator.conf \
	&& cd sst-elements \
	&& ./configure \
	&& make install  

# Apply SST fix
WORKDIR /social-network-microservices 
RUN git clone https://github.com/ansaschmulbach/sst-elements.git ansa-sst \
	&& ls \
	&& cp ansa-sst/src/sst/elements/prospero/tracetool/Makefile sst-elements/src/sst/elements/prospero/tracetool/ \
	&& cp ansa-sst/src/sst/elements/prospero/tracetool/sstmemtrace.cc sst-elements/src/sst/elements/prospero/tracetool/ \
	&& cp ansa-sst/src/sst/elements/prospero/tests/array/trace-common.py sst-elements/src/sst/elements/prospero/tests/array/trace-common.py \
	&& cd sst-elements/src/sst/elements/prospero/tracetool/ \
	&& export INTEL_PIN_DIRECTORY=/social-network-microservices/pin 
#	&& make

WORKDIR /social-network-microservices
