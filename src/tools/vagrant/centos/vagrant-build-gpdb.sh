#!/bin/bash
set -x

while [ $# -gt 0 ]; do
  case "$1" in
    --enable-orca)
      enable_orca="${1}"
      ;;
    --build-local)
      build_local="${1}"
      ;;
    *)
      printf "Error: Unknown Flag: $1\n"
      printf "Optional Flags: [--build-local] [--enable-orca]\n"
      exit 1
  esac
  shift
done

rm -rf /usr/local/gpdb

pushd ~
  rm -fr gpdb
  rm -fr gpos
  rm -fr gp-xerces
  rm -fr gporca

  if [ -z "$build_local" ]; then
       git clone https://github.com/greenplum-db/gpdb
  else
    ln -s /gpdb ~/gpdb
  fi
popd

export CC="ccache cc"
export CXX="ccache c++"
export PATH=/usr/local/bin:$PATH

if [ -z "$enable_orca" ]; then
  pushd ~
    git clone https://github.com/greenplum-db/gpos #-- not required?
    git clone https://github.com/greenplum-db/gp-xerces
    git clone https://github.com/greenplum-db/gporca
  popd

  pushd ~/gpos
    rm -fr build
    mkdir build
    pushd build
      cmake ../
      make -j4 && make install
    popd
  popd

  pushd ~/gp-xerces
    rm -fr build
    mkdir build
    pushd build
      ../configure --prefix=/usr/local
      make -j4 && make install
    popd
  popd

  pushd ~/gporca
    rm -fr build
    mkdir build
    pushd build
      cmake ../
      make -j4 && make install
    popd
  popd
fi

pushd ~/gpdb
  ./configure --prefix=/usr/local/gpdb $enable_orca
  if [ $? -ne 0 ]; then
    printf "Configure Failed: Exiting"
    # To-Do: Provide Instructions for Re-run. ? If local fails use git? # Can we git the right verison for local?
    exit 1;
  fi
  make clean
  make -j4 -s && make install
popd

# BUG: fix the LD_LIBRARY_PATH to find installed GPOPT libraries -- now in vagrant-configure-os
# echo export LD_LIBRARY_PATH=/usr/local/lib:\$LD_LIBRARY_PATH >> /usr/local/gpdb/greenplum_path.sh

# use gpdemo to start the cluster
pushd ~/gpdb/gpAux/gpdemo
  source /usr/local/gpdb/greenplum_path.sh
  make
popd
