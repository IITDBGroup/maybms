########################################
# Derived from ubuntu distribution
########################################
FROM ubuntu:12.04

########################################
# Install packages needed for building
########################################
RUN apt-get -y update && apt-get install -y \
    gcc \
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
# Create user (other than root to be able to start postgres
########################################
RUN useradd -m maybms && echo "maybms:maybms" | chpasswd && adduser maybms sudo
USER maybms
WORKDIR /home/maybms
########################################
# Clone Maybms repro
########################################
RUN git clone https://github.com/IITDBGroup/maybms.git /home/maybms/src
########################################
# Patch and build postgres
########################################
RUN cd /home/maybms/src \
    && ./pip_plugin/scripts/patch_postgres.sh
RUN cd /home/maybms/src/postgresql-8.3.3 \
    && ./configure --prefix=/home/maybms/install \
    && make -j16 \
    && make install \
    && cp -r /home/maybms/src/postgresql-8.3.3/src/include/maybms /home/maybms/install/include/postgresql/server/maybms/
########################################
# Environment
########################################
ENV PGDATA /home/maybms/datadir
ENV PATH /home/maybms/install/bin:$PATH
########################################
# Expose postmaster port
########################################
EXPOSE 5432
########################################
# build pip functions
########################################
RUN cd /home/maybms/src/pip_plugin && make && make install
########################################
# Add datadir as volume to persist across container restarts
########################################
RUN ["/home/maybms/src/docker/createDB.sh","maybms"]
########################################
# Per default start postgres
########################################
ENTRYPOINT ["/home/maybms/install/bin/postgres"]
########################################
# Per default connect to the example cluster
########################################
CMD ["-D", "/home/maybms/datadir"]
VOLUME /home/maybms/datadir
