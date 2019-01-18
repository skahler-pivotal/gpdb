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
   #need to throw an error handle in here...
  ./configure --prefix=/usr/local/gpdb $enable_orca
  make clean
  make -j4 -s && make install
popd

# generate ssh key to avoid typing password all the time during gpdemo make
rm -f ~/.ssh/id_rsa
rm -f ~/.ssh/id_rsa.pub
ssh-keygen -t rsa -N "" -f "$HOME/.ssh/id_rsa"
cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys
chmod 600 ~/.ssh/authorized_keys

# BUG: fix the LD_LIBRARY_PATH to find installed GPOPT libraries
echo export LD_LIBRARY_PATH=/usr/local/lib:\$LD_LIBRARY_PATH >> /usr/local/gpdb/greenplum_path.sh

# use gpdemo to start the cluster
pushd $build_path/gpAux/gpdemo
  source /usr/local/gpdb/greenplum_path.sh
  make
popd
