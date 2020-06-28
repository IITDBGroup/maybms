########################################
# Derived from ubuntu distribution
########################################
FROM ubuntu:14.04

########################################
# Install packages needed for building
########################################
RUN apt-get -y update && apt-get install -y \
	autotools-dev \
	autoconf \
	bison \
	flex \
    git \
    rcs \
	libtool \
	make \
	libreadline6-dev \
	libxml2 \
	libxml2-dev \
	libxslt1-dev \
	sudo \
	zlib1g-dev \
	&& rm -rf /var/lib/apt/lists/*
########################################
# Clone Maybms repro
########################################
RUN git clone https://github.com/IITDBGroup/maybms.git /maybms
WORKDIR /maybms
########################################
# Patch and build postgres
########################################
RUN cd /maybms && ./pip_plugin/scripts/patch_postgres.sh
RUN cd /maybms/postgresql-ctype \
    && ./configure --prefix=/maybms/install \
    && make -j16 \
    && make install \
    && mkdir /data \
    && cp -r /maybms/postgresql-ctype/src/include/maybms /maybms/install/include/postgresql/server/maybms/
########################################
# Environment
########################################
ENV PGDATA /data
ENV PATH /maybms/install/bin:$PATH
########################################
# Expose postmaster port
########################################
EXPOSE 5432
########################################
# build pip functions
########################################
RUN cd /maybms/pip_plugin && make && make install
########################################
# Add datadir as volume to persist across container restarts
########################################
VOLUME /data
RUN mkdir /data
RUN /maybms/install/bin/initdb -D /data
########################################
# Per default start postgres
########################################
ENTRYPOINT ["/maybms/install/bin/postgres"]
########################################
# Per default connect to the example cluster
########################################
CMD ["-D", "/data"]
