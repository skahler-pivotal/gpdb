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


pushd ~
  if [ -z "$build_local" ]; then
       rm -fr gpdb
       git clone https://github.com/greenplum-db/gpdb
  else
    ln -s /gpdb ~/gpdb
  fi
popd

export CC="ccache cc"
export CXX="ccache c++"
export PATH=/usr/local/bin:$PATH

rm -rf /usr/local/gpdb

pushd ~/gpdb
  ./configure --prefix=/usr/local/gpdb $enable_orca
  if [ $? -ne 0 ]; then
    printf "Configure Failed: Exiting"
    # To-Do: Provide Instructions for Re-Run
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
