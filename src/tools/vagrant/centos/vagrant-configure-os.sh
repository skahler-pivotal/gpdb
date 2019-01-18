#!/bin/bash
set -x

# Stop firewall
sudo systemctl stop firewalld

# Disable SELinux
sudo setenforce 0
sudo sed -i 's/^SELINUX=.*$/SELINUX=disabled/' /etc/selinux/config

# GPDB Kernel Settings
sudo rm -f /etc/sysctl.d/gpdb.conf
sudo bash -c 'printf "# GPDB-Specific Settings\n\n"                    >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.shmmax = 500000000\n"                     >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.shmmni = 4096\n"                          >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.shmall = 4000000000\n"                    >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.sem = 500 1024000 200 4096\n"             >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.sysrq = 1\n"                              >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.core_uses_pid = 1\n"                      >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.msgmnb = 65536\n"                         >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.msgmax = 65536\n"                         >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.msgmni = 2048\n"                          >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv4.tcp_syncookies = 1\n"                   >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv4.ip_forward = 1\n"                       >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv4.conf.default.accept_source_route = 0\n" >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv4.tcp_tw_recycle = 1\n"                   >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv4.tcp_max_syn_backlog = 4096\n"           >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv4.conf.all.arp_filter = 1\n"              >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv4.ip_local_port_range = 1025 65535\n"     >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.ipv6.conf.all.disable_ipv6 = 1\n             >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.core.netdev_max_backlog = 10000\n"           >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.core.rmem_max = 2097152\n"                   >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "net.core.wmem_max = 2097152\n"                   >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "vm.overcommit_memory = 2\n"                      >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.core_uses_pid = 1\n"                      >> /etc/sysctl.d/gpdb.conf'
sudo bash -c 'printf "kernel.core_pattern = /tmp/gpdb_cores/core-%%e-%%s-%%u-%%g-%%p-%%t\n" >> /etc/sysctl.d/gpdb.conf'
sudo sysctl -p /etc/sysctl.d/gpdb.conf

# Creating directory for core files
sudo rm -rf /tmp/gpdb_cores
sudo mkdir -p /tmp/gpdb_cores
sudo chown vagrant:vagrant /tmp/gpdb_cores

# GPDB Kernel Limits
sudo rm -f /etc/security/limits.d/gpdb.conf
sudo bash -c 'printf "# GPDB-Specific Settings\n\n"     >> /etc/security/limits.d/gpdb.conf'
sudo bash -c 'printf "* soft nofile 65536\n"            >> /etc/security/limits.d/gpdb.conf'
sudo bash -c 'printf "* hard nofile 65536\n"            >> /etc/security/limits.d/gpdb.conf'
sudo bash -c 'printf "* soft nproc 131072\n"            >> /etc/security/limits.d/gpdb.conf'
sudo bash -c 'printf "* hard nproc 131072\n"            >> /etc/security/limits.d/gpdb.conf'
sudo bash -c 'printf "* soft core unlimited\n"         >> /etc/security/limits.d/gpdb.conf'
sudo bash -c 'printf "* hard core unlimited\n"         >> /etc/security/limits.d/gpdb.conf'

# Do not destroy user context on logout
sudo sed -i '/RemoveIPC=no/d' /etc/systemd/logind.conf
sudo bash -c 'echo "RemoveIPC=no" >> /etc/systemd/logind.conf'
sudo service systemd-logind restart

# Change Ownership
sudo chown -R vagrant:vagrant /usr/local

# Update LD_LIBRARY_PATH
sudo bash -c 'echo "/usr/local/lib" > /etc/ld.so.conf.d/local.conf'
sudo bash -c 'ldconfig'

# Setup alteratives for cmake, using cmake3 by default
sudo bash -c 'alternatives --install /usr/local/bin/cmake cmake /usr/bin/cmake 10 \
--slave /usr/local/bin/ctest ctest /usr/bin/ctest \
--slave /usr/local/bin/cpack cpack /usr/bin/cpack \
--slave /usr/local/bin/ccmake ccmake /usr/bin/ccmake \
--family cmake'

sudo bash -c 'alternatives --install /usr/local/bin/cmake cmake /usr/bin/cmake3 20 \
--slave /usr/local/bin/ctest ctest /usr/bin/ctest3 \
--slave /usr/local/bin/cpack cpack /usr/bin/cpack3 \
--slave /usr/local/bin/ccmake ccmake /usr/bin/ccmake3 \
--family cmake'

# Generate ssh key to avoid typing password for managment tools and utilities
rm -f ~/.ssh/id_rsa
rm -f ~/.ssh/id_rsa.pub
ssh-keygen -t rsa -N "" -f "$HOME/.ssh/id_rsa"
cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys
chmod 600 ~/.ssh/authorized_keys
