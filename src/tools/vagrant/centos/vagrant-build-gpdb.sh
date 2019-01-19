#!/bin/bash
# set -x
export CC="ccache cc"
export CXX="ccache c++"
export PATH=/usr/local/bin:$PATH

# Check Flags
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

# Cleanup
pushd ~
  rm -fr gpdb
  rm -fr gpos
  rm -fr gp-xerces
  rm -fr gporca
  rm -fr /usr/local/gpdb
popd

# Build ORCA
if [ -n "$enable_orca" ]; then

  printf "\n\nORCA Enabled:\n"
  pushd ~
    git clone https://github.com/greenplum-db/gp-xerces
    git clone https://github.com/greenplum-db/gporca
  popd

  printf "\n\nBuilding gp-xerces:\n"
  pushd ~/gp-xerces
    mkdir build
    pushd build
      ../configure --prefix=/usr/local  # cmake -G Ninja -H. -B build
      make -j4 && make install          # ninja-build install -C build
    popd
  popd

  printf "\n\nBuilding gporca:\n"
  pushd ~/gporca
     git pull --ff-only
     cmake -G Ninja -H. -B build
     ninja-build install -C build
  popd

  # Post-Optimizer: Update Shared Libraries
  sudo bash -c 'ldconfig'
fi

# Build GPDB
pushd ~
  if [ -n "$build_local" ]; then
    printf "\n\nBuilding Local: ~/gpdb\n"
    ln -s /gpdb ~/gpdb
  else
    printf "\n\nBuilding Remote: https://github.com/greenplum-db/gpdb\n"
    git clone https://github.com/greenplum-db/gpdb
  fi

  # To-Do: Option gcc-6
  # sudo yum install -y centos-release-scl
  # sudo yum install -y devtoolset-6-toolchain
  # echo 'source scl_source enable devtoolset-6' >> ~/.bashrc

  printf "\n\nBuilding gpdb:\n"
  pushd ~/gpdb
    ./configure -with-perl --with-python --with-libxml --with-gssapi --prefix=/usr/local/gpdb $enable_orca

    if [ $? -ne 0 ]; then
      printf "\n\nConfigure Failed: Exiting\n\n"
      # To-Do: Provide Instructions for Re-run. ? If local fails use git? # Can we git the right verison for local?
      exit 1;
    fi

    make clean
    make -j8 && make -j8 install
  popd
popd

# BUG: fix the LD_LIBRARY_PATH to find installed GPOPT libraries -- now in vagrant-configure-os
# echo export LD_LIBRARY_PATH=/usr/local/lib:\$LD_LIBRARY_PATH >> /usr/local/gpdb/greenplum_path.sh
# Use gpdemo to start the cluster

source /usr/local/gpdb/greenplum_path.sh
