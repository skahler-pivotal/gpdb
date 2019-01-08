#!/bin/bash

set -ex

# clone the repos
rm -fr gpos
rm -fr gp-xerces
rm -fr gporca
rm -fr gpdb

pushd ~
  git clone https://github.com/greenplum-db/gpos
  git clone https://github.com/greenplum-db/gp-xerces
  git clone https://github.com/greenplum-db/gporca
  git clone https://github.com/greenplum-db/gpdb
popd

export CC="ccache cc"
export CXX="ccache c++"
export PATH=/usr/local/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

pushd ~/gpos
  rm -fr build
  mkdir build
  pushd build
    cmake ../
    make -j4 && sudo make install
  popd
popd

pushd ~/gp-xerces
  rm -fr build
  mkdir build
  pushd build
    ../configure --prefix=/usr/local
    make -j4 && sudo make install
  popd
popd

pushd ~/gporca
  rm -fr build
  mkdir build
  pushd build
    cmake ../
    make -j4 && sudo make install
  popd
popd

rm -rf /usr/local/gpdb
sudo mkdir /usr/local/gpdb
sudo chown vagrant:vagrant /usr/local/gpdb

pushd ~/gpdb
  ./configure --prefix=/usr/local/gpdb CFLAGS="-I/usr/local/include/ -L/usr/local/lib/" $@
  make clean
  make -j4 -s && sudo make install
popd

sudo chown -R vagrant:vagrant /usr/local/gpdb

cat /home/vagrant/.ssh/id_rsa.pub >> /home/vagrant/.ssh/authorized_keys
# make sure ssh is not stuck asking if the host is known
ssh-keyscan -H localhost >> /home/vagrant/.ssh/known_hosts
ssh-keyscan -H 127.0.0.1 >> /home/vagrant/.ssh/known_hosts

# BUG: fix the LD_LIBRARY_PATH to find installed GPOPT libraries
echo export LD_LIBRARY_PATH=/usr/local/lib:\$LD_LIBRARY_PATH >> /usr/local/gpdb/greenplum_path.sh

# use gpdemo to start the cluster
pushd ~/gpdb/gpAux/gpdemo
  source /usr/local/gpdb/greenplum_path.sh
  make
popd
