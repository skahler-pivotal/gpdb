#!/bin/bash
set -x

# install packages needed to build and run GPDB
sudo yum -y groupinstall "Development tools"
sudo yum -y install epel-release
sudo yum -y install \
 apr-devel \
 bzip2-devel \
 ccache \
 cmake3 \
 curl-devel \
 ed \
 htop \
 libevent-devel \
 libffi-devel \
 libxml2 \
 libxml2-devel \
 libyaml \
 libyaml-devel \
 mc \
 openssl-libs \
 openssl-devel \
 perl-Env \
 psmisc \
 python-devel \
 python-pip \
 readline-devel \
 vim \
 zlib-devel

# Install necessary Python pieces
sudo pip install --upgrade psutil
sudo pip install --upgrade lockfile
sudo pip install --upgrade paramiko
sudo pip install --upgrade setuptools
sudo pip install --upgrade epydoc
sudo pip install --upgrade pyyaml

# cmake 3.1
pushd ~
  wget http://www.cmake.org/files/v3.1/cmake-3.1.0.tar.gz
  tar -zxvf cmake-3.1.0.tar.gz
  pushd cmake-3.1.0
    ./bootstrap
    make
    make install
    export PATH=/usr/local/bin:$PATH
  popd
popd

# Misc -- perhaps move to configure-os?
sudo chown -R vagrant:vagrant /usr/local
sudo bash -c 'echo "/usr/local/lib" > /etc/ld.so.conf.d/local.conf'
