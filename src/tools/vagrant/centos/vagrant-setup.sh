#!/bin/bash
# set -x

# install packages needed to build and run GPDB
sudo yum -y groupinstall "Development tools"
sudo yum -y install epel-release
sudo yum -y install \
 apr-devel \
 bzip2-devel \
 ccache \
 cmake3 \
 ed \
 htop \
 krb5-devel \
 libcurl-devel \
 libevent-devel \
 libffi-devel \
 libkadm5 \
 libxml2 \
 libxml2-devel \
 libyaml \
 libyaml-devel \
 mc \
 ninja-build \
 openssl-libs \
 openssl-devel \
 perl-Env \
 perl-ExtUtils-Embed \
 psmisc \
 python-devel \
 python-pip \
 readline-devel \
 vim \
 xerces-c-devel \
 zlib-devel

# Install necessary Python pieces
# sudo pip install --upgrade conan
sudo pip install --upgrade psutil
sudo pip install --upgrade lockfile
sudo pip install --upgrade paramiko
sudo pip install --upgrade setuptools
sudo pip install --upgrade epydoc
sudo pip install --upgrade pyyaml
