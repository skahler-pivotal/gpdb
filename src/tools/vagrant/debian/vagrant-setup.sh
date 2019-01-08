sudo apt-get update
sudo apt-get install wget
sudo apt-get -y install \
  bison \
  build-essential \
  clang-4.0 \
  ccache \
  cmake \
  flex \
  git-core \
  libapr1-dev \
  libbz2-dev  \
  libcurl4-openssl-dev \
  libevent-dev \
  libffi-dev \
  libperl-dev \
  libreadline-dev \
  libssl-dev \
  libxml2-dev \
  libyaml-dev \
  pkg-config \
  python-dev \
  python-pip \
  vim \
  zlib1g-dev 

echo locales locales/locales_to_be_generated multiselect     de_DE ISO-8859-1, de_DE ISO-8859-15, de_DE.UTF-8 UTF-8, de_DE@euro ISO-8859-15, en_GB ISO-8859-1, en_GB ISO-8859-15, en_GB.ISO-8859-15 ISO-8859-15, en_GB.UTF-8 UTF-8, en_US ISO-8859-1, en_US ISO-8859-15, en_US.ISO-8859-15 ISO-8859-15, en_US.UTF-8 UTF-8 | debconf-set-selections
echo locales locales/default_environment_locale      select  en_US.UTF-8 | debconf-set-selections
dpkg-reconfigure locales -f noninteractive

su vagrant -c "ssh-keygen -t rsa -f .ssh/id_rsa -q -N ''"

sudo pip install --upgrade psutil
sudo pip install --upgrade lockfile
sudo pip install --upgrade paramiko
sudo pip install --upgrade setuptools
sudo pip install --upgrade epydoc
sudo pip install --upgrade pyyaml
