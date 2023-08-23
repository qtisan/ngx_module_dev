## Getting Started

**Make a workplace directory, and clone the nginx source code.**

```sh
mkdir nginx_module_dev
cd nginx_module_dev
git clone <the git repo>
```

**Clone the nginx source code.**

```sh
git clone https://github.com/nginx/nginx.git
```

**Clone the dependencies.**

```sh
# pcre2
git clone https://github.com/PCRE2Project/pcre2.git

# zlib
git clone https://github.com/madler/zlib.git

# openssl
git clone https://github.com/openssl/openssl.git

```

**CAUTION: To compile with pcre2, you should gen the `configure` script first.**

```sh
cd pcre2
./autogen.sh
```

see the README of pcre2 for more details.
> Building PCRE2 using autotools
> 
> ------------------------------

> The following instructions assume the use of the widely used "configure; make;
> make install" (autotools) process.
> 
> If you have downloaded and unpacked a PCRE2 release tarball, run the
> "configure" command from the PCRE2 directory, with your current directory set
> to the directory where you want the files to be created. This command is a
> standard GNU "autoconf" configuration script, for which generic instructions
> are supplied in the file INSTALL.
> 
> The files in the GitHub repository do not contain "configure". If you have
> downloaded the PCRE2 source files from GitHub, before you can run "configure"
> you must run the shell script called autogen.sh. This runs a number of
> autotools to create a "configure" script (you must of course have the autotools
> commands installed in order to do this).


## With Devcontainer

Use the following command to start the dev container, with the `compose-dev.yaml` file.
Change the congifurations of:

- image
- volumes bind

```
docker-compose -f compose-dev.yaml up -d
```

## Presets

- Environment variable `WORKPLACE` is set to the path of the workplace, in `~/.bashrc`.

```sh
export WORKPLACE=/nginx_module_dev
```

- The custom module is placed in the path `$WORKPLACE/ngx_http_save_ctx_2store`.
- Prefix of nginx is set to `/usr/local/nginx`.

replace the `apt-get` sources to `aliyun`:

```sh
# backup the sources.list
sudo cp /etc/apt/sources.list /etc/apt/sources.list.bak

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

# update the apt-get
sudo apt-get update

# install the c compiler
sudo apt-get install build-essential

# install gdb for debug
sudo apt-get install gdb

```

### Play with VSCode

- Update the `miDebuggerPath` field, to the `gdb` executable file in `.vscode/launch.json` if necessary.

- Update the `C_Cpp.default.compilerPath` field, to the `gcc` executable file in `.vscode/settings.json` if necessary.

Next:

1. Install the `Remote - Containers` extension.
2. Connect to the dev container.
3. Open the `nginx.code-workspace` with VSCode.

The project `conf` and `logs` indicates the nginx conf and logs directory, for debugging.


## Development


### Configure Script

The custom module is added to the configure dynamically script by the following command:

**at the nginx source code root directory**

```sh
cd $WORKPLACE/nginx

# Clean first
make clean

# Add the custom module to the configure script
./auto/configure \
    --with-compat \
    --with-debug \
    --prefix=/usr/local/nginx \
    --with-http_realip_module \
    --with-stream \
    --with-http_stub_status_module \
    --with-http_secure_link_module \
    --with-http_addition_module \
    --with-pcre=$WORKPLACE/pcre2 \
    --with-zlib=$WORKPLACE/zlib \
    --with-openssl=$WORKPLACE/openssl \
    --add-dynamic-module=$WORKPLACE/ngx_http_save_ctx_2store

# Compile the nginx
make

# Install the nginx
make install

# Start the nginx
/usr/local/nginx/sbin/nginx
```

> **Should remove the `--with-debug` option in the production environment.**

### Recompile

1. Modify some code in the custom module.
2. Recompile the custom module.

```sh
make modules
```

3. Move the compiled module to the nginx modules directory.

```sh
make install
```

4. Restart nginx.

```sh
/usr/local/nginx/sbin/nginx -s reload
```

### Example `nginx.conf`

Add the example `nginx.conf` to the nginx conf directory, and restart the nginx.

```conf
# add the module at the top of the configure file
load_module modules/ngx_http_save_ctx_2store_module.so;

worker_processes  1;
error_log  logs/error.log  debug;
events {
    worker_connections  1024;
}

http {
    include             mime.types;
    default_type        application/octet-stream;
    sendfile            on;
    keepalive_timeout   65;

    upstream  real.api {
        server 127.0.0.1:8080;
        keepalive 10;
    }

    server {
        listen       80;
        server_name  localhost;

        # define a connection string
        save_ctx_2store_conn mysql://root:root@localhost:3306/ctx_2store;

        location / {

            # active storing context to the connection string
            save_ctx_2store;

            proxy_pass http://real.api;
            proxy_http_version 1.1;
            proxy_set_header Connection "keep-alive";
        }
    }

    server {
        listen       8080;

        location / {
            root   html;
            index  index.html index.htm;
        }
    }
}
```


### Enable Debugging

**Debug on nginx startup**:

- Add breakpoints to the source code, `$WORKPLACE/ngx_http_save_ctx_2store/save_ctx_2store_module.c`.
    Only the breakpoints in the startup cycle functions will be triggered.
- Use the VSCode debugging tool to startup the nginx process.
    At the `Run and Debug` Panel, select `Nginx Start` script, click the `Start Debugging` button.

**Debug on nginx running**:

Attach the GDB to the running nginx process.

**IMPORTANT:**


see [Cannot attach to the process with GDB, at dev container.](#gdb-attach) for more details.

- Add breakpoints to the source code, `handler` functions.
- At the `Run and Debug` Panel, select `(gdb)Attach` script, click the `Start Debugging` button.
- Input the `pid` of the running nginx process, and press `Enter`.
- Enjoy the debugging.

## Touble Shooting

<a id="gdb-attach" />
### Cannot attach to the process with GDB, at dev container.

```
Unable to start debugging. Attaching to process <process-id> with GDB failed because of insufficient privileges with error message 'ptrace: Operation not permitted.'.

To attach to an application on Linux, login as super user or set ptrace_scope to 0.
See https://aka.ms/miengine-gdb-troubleshooting for details.
```

> Attaching to a process on Linux with GDB as a normal user may fail with "ptrace:Operation not permitted". By default Linux does not allow attaching to a process which wasn't launched by the debugger (see the Yama security documentation for more details).
>
> There are three ways to workaround this:
> Run the following command as super user: echo 0| sudo tee /proc/sys/kernel/yama/ptrace_scope
>
> This will set the ptrace level to 0, after this just with user permissions you can attach to processes which are not launched by the debugger.
> On distributions without Yama (such as Raspbian) you can use libcap2-bin to assign ptrace permissions to specific executables: sudo setcap cap_sys_ptrace=eip /usr/bin/gdb
>
> Alternatively, launch GDB as super user and attach to processes. Use root@machine to login with a password or certificate. Note, many Linux distros are configured to disallow root login through SSH for security reasons. You may have to configure /etc/ssh/sshd_config to allow root login with certificate or password. You can verify SSH connection via a client like putty.exe in windows.
>
> **Docker containers**
>
> For Docker Linux containers, it is necessary to add the capability when the container is created in order to allow attaching to a process with gdb.
>
> The flag of --cap-add=SYS_PTRACE needs to be added when starting the container using docker run.
>
> More information can be found on the Docker documentation page.

**Privileged to attach the GDB to the running nginx process:**

See at `README.sysctl`

> Kernel system variables configuration files
>
> Files found under the /etc/sysctl.d directory that end with .conf are
> parsed within sysctl(8) at boot time. If you want to set kernel variables
> you can either edit /etc/sysctl.conf or make a new file.
> 
> The filename isn't important, but don't make it a package name as it may clash
> with something the package builder needs later. It must end with .conf though.
> 
> My personal preference would be for local system settings to go into
> /etc/sysctl.d/local.conf but as long as you follow the rules for the names
> of the file, anything will work. See sysctl.conf(8) man page for details
> of the format.

