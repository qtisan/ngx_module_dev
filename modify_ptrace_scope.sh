#!/bin/bash

file_path="/etc/sysctl.d/10-ptrace.conf"
ptrace_text="kernel.yama.ptrace_scope=0"

echo "Modify the ptrace scope..." &&\
echo "replace the ptrace scope to 0..."
if [ ! -f "$file_path" ]; then
    echo "$ptrace_text" | sudo tee "$file_path" > /dev/null
else
    if grep -q "kernel.yama.ptrace_scope=1" "$file_path"; then
        sudo sed -i 's/kernel.yama.ptrace_scope=1/kernel.yama.ptrace_scope=0/' "$file_path"
    elif grep -q "kernel.yama.ptrace_scope=0" "$file_path"; then
        echo "kernel.yama.ptrace_scope=0 already exists in $file_path. No changes needed."
    else
        echo "$ptrace_text" | sudo tee -a "$file_path" > /dev/null
    fi
fi

echo "reload the ptrace scope..." &&\
sudo sysctl -p "$file_path" &&\
echo "ptrace scope modified to 0."
