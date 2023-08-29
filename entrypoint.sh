#!/bin/bash

nginx_path_prefix="/usr/local/nginx"

if [ -f ~/.bashrc ]; then
    if ! grep -q "export WORKPLACE=/ngx_module_dev" ~/.bashrc; then
        echo "export WORKPLACE=/ngx_module_dev" >> ~/.bashrc &&\
        echo "[✓] Added 'export WORKPLACE=/ngx_module_dev' to ~/.bashrc"
    else
        echo "Line 'export WORKPLACE=/ngx_module_dev' already exists in ~/.bashrc"
    fi

    . ~/.bashrc
else
    echo "~/.bashrc file not found"
fi
echo "[✓] \$WORKPLACE has been set to $WORKPLACE." &&\

. $WORKPLACE/modify_ptrace_scope.sh &&\

if [ ! -x "/usr/local/bin/starship" ]; then
    echo "start downloading starship..." &&\
    yes | curl -sS https://starship.rs/install.sh | sh -s -- --yes &&\
    echo "starship installed." &&\
    echo "eval \"\$(starship init bash)\"" >> ~/.bashrc &&\
    . ~/.bashrc
fi
echo "[✓] starship has been installed." &&\

if ! grep -q "deb https://mirrors.aliyun.com/debian/ bullseye main contrib non-free" /etc/apt/sources.list; then
        
    echo "[*] update the apt-get..." &&\
    # backup the sources.list
    sudo cp /etc/apt/sources.list /etc/apt/sources.list.bak &&\

    # replace the sources.list with aliyun, debian 11.4
cat > /etc/apt/sources.list << EOF
deb https://mirrors.aliyun.com/debian/ bullseye main contrib non-free
deb-src https://mirrors.aliyun.com/debian/ bullseye main contrib non-free
deb https://mirrors.aliyun.com/debian/ bullseye-updates main contrib non-free
deb-src https://mirrors.aliyun.com/debian/ bullseye-updates main contrib non-free
deb https://mirrors.aliyun.com/debian/ bullseye-backports main contrib non-free
deb-src https://mirrors.aliyun.com/debian/ bullseye-backports main contrib non-free
deb https://mirrors.aliyun.com/debian-security/ bullseye-security main contrib non-free
deb-src https://mirrors.aliyun.com/debian-security/ bullseye-security main contrib non-free
EOF
    
        echo "[✓] sources.list has been updated."
fi

# update the apt-get
sudo apt-get update -y &&\

# install the c compiler
if ! dpkg -s build-essential >/dev/null 2>&1; then
    echo "[*] install the build-essential..." &&\
    sudo apt-get install -y build-essential &&\
    echo "[✓] build-essential has been installed."
fi

# install gdb for debug
if ! dpkg -s gdb >/dev/null 2>&1; then
    echo "[*] install the gdb..." &&\
    sudo apt-get install -y gdb &&\
    echo "[✓] gdb has been installed."
fi

# install cmake for debug
if ! dpkg -s cmake >/dev/null 2>&1; then
    echo "[*] install the cmake..." &&\
    sudo apt-get install -y cmake &&\
    echo "[✓] cmake has been installed."
fi

if [ ! -d "$WORKPLACE/nginx" ]; then
    echo "[*] clone the nginx..." &&\
    git clone https://github.com/nginx/nginx.git $WORKPLACE/nginx &&\
    echo "[✓] nginx has been cloned."
fi

if [ ! -d "$WORKPLACE/pcre2" ]; then
    echo "[*] clone the pcre2..." &&\
    git clone https://github.com/PCRE2Project/pcre2.git $WORKPLACE/pcre2 &&\
    echo "[✓] pcre2 has been cloned." &&\
    cd $WORKPLACE/pcre2 && ./autogen.sh &&\
    echo "[✓] pcre2 autogen runs successfully."
fi

if [ ! -d "$WORKPLACE/zlib" ]; then
    echo "[*] clone the zlib..." &&\
    git clone https://github.com/madler/zlib.git $WORKPLACE/zlib &&\
    echo "[✓] zlib has been cloned."
fi

if [ ! -d "$WORKPLACE/openssl" ]; then
    echo "[*] clone the openssl..." &&\
    git clone https://github.com/openssl/openssl.git $WORKPLACE/openssl &&\
    echo "[✓] openssl has been cloned."
fi

cd $WORKPLACE/nginx &&\

# Add the custom module to the configure script
if [ ! -d "$WORKPLACE/nginx/objs" ]; then
    make clean &&\

    echo "[*] start to compile the nginx..." &&\    
    ./auto/configure \
        --with-compat \
        --with-debug \
        --prefix=$nginx_path_prefix \
        --with-http_realip_module \
        --with-stream \
        --with-http_stub_status_module \
        --with-http_secure_link_module \
        --with-http_addition_module \
        --with-pcre=$WORKPLACE/pcre2 \
        --with-zlib=$WORKPLACE/zlib \
        --with-openssl=$WORKPLACE/openssl \
        --add-dynamic-module=$WORKPLACE/ngx_http_save_ctx_2store &&\

    # Compile the nginx
    make
fi

echo "[*] installing the nginx..." &&\

# Install the nginx
make install &&\
echo "[✓] nginx has been installed." &&\

make modules OPTIMIZATION=O0 &&\
echo "[✓] modules has been compiled, with no optimization." &&\

if [ -f "$nginx_path_prefix/conf/nginx.conf" ]; then
    echo "[*] backup the nginx.conf..." &&\
    cp $nginx_path_prefix/conf/nginx.conf $nginx_path_prefix/conf/nginx.conf.bak &&\
    echo "[✓] nginx.conf has been backed up."
fi

# Set the example config file
cp $WORKPLACE/ngx_http_save_ctx_2store/nginx.example.conf $nginx_path_prefix/conf/nginx.conf &&\

echo "[✓] replaced the example config file..." &&\

echo "[*] start to run the nginx..." &&\

# Start the nginx
/usr/local/nginx/sbin/nginx &&\

echo "[✓] nginx started." &&\

if [[ -e "/usr/bin/nginx" ]]; then
    rm /usr/bin/nginx
fi

ln -s /usr/local/nginx/sbin/nginx /usr/bin/nginx

if [[ -e "/usr/bin/nginx" ]]; then
    echo "[✓] created link /usr/bin/nginx"
else
    echo "[ERR] failed to create /usr/bin/nginx!"
fi

echo "[✓] ALL DONE!" &&\

tail -f /dev/null