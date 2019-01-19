#!/bin/bash
set -x

# Stop firewall
sudo systemctl stop firewalld

# Disable SELinux
sudo setenforce 0
sudo sed -i 's/^SELINUX=.*$/SELINUX=disabled/' /etc/selinux/config

# GPDB Kernel Settings
sudo rm -f /etc/sysctl.d/gpdb.conf
sudo bash -c 'cat >> /etc/sysctl.d/gpdb.conf <<-EOF
# GPDB-Specific Settings
kernel.core_pattern = /tmp/gpdb_cores/core-%%e-%%s-%%u-%%g-%%p-%%t
kernel.core_uses_pid = 1
kernel.msgmax = 65536
kernel.msgmnb = 65536
kernel.msgmni = 2048
kernel.sem = 500 1024000 200 4096
kernel.shmall = 4000000000
kernel.shmmax = 500000000
kernel.shmmni = 4096
kernel.sysrq = 1
net.core.netdev_max_backlog = 10000
net.core.rmem_max = 2097152
net.core.wmem_max = 2097152
net.ipv4.conf.all.arp_filter = 1
net.ipv4.conf.default.accept_source_route = 0
net.ipv4.ip_forward = 0
net.ipv4.ip_local_port_range = 1025 65535
net.ipv4.tcp_max_syn_backlog = 4096
net.ipv4.tcp_syncookies = 1
net.ipv4.tcp_tw_recycle = 1
net.ipv6.conf.all.disable_ipv6 = 1
vm.overcommit_memory = 2
EOF'
sudo sysctl -p /etc/sysctl.d/gpdb.conf

# GPDB Kernel Limits
sudo rm -f /etc/security/limits.d/gpdb.conf
sudo bash -c 'cat >> /etc/security/limits.conf <<-EOF
# GPDB-Specific Settings
* soft nofile 65536
* hard nofile 65536
* soft nproc 131072
* hard nproc 131072
* soft core unlimited
* hard core unlimited
EOF'

# Do not destroy user context on logout
sudo sed -i '/RemoveIPC=no/d' /etc/systemd/logind.conf
sudo bash -c 'echo "RemoveIPC=no" >> /etc/systemd/logind.conf'
sudo service systemd-logind restart

# File System Updates
sudo rm -rf /tmp/gpdb_cores
sudo mkdir -p /tmp/gpdb_cores
sudo chown -R vagrant:vagrant /tmp/gpdb_cores
sudo chown -R vagrant:vagrant /usr/local

# Update ldconfig
sudo bash -c 'cat >> /etc/ld.so.conf <<-EOF
/usr/local/lib
/usr/local/lib64
EOF'
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
