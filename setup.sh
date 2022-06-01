echo -e "\033[41;37m 正在安装必要软件包 \033[0m"
sudo apt update
sudo apt install -y build-essential gettext pkg-config libgmp-dev libglib2.0-dev python3-dev libpixman-1-dev binutils libgtk-3-dev texinfo make gcc-riscv64-linux-gnu libncurses5-dev ninja-build tmux axel git
echo -e "\033[41;37m 正在下载必要源码包 \033[0m"
cd resource
axel -n 8 https://download.qemu.org/qemu-7.0.0.tar.xz
axel -n 8 https://mirror.lzu.edu.cn/gnu/gdb/gdb-12.1.tar.xz
echo -e "\033[41;37m 正在解压 \033[0m"
tar xJf qemu-7.0.0.tar.xz
tar xJf gdb-12.1.tar.xz
echo -e "\033[41;37m 解压完成 \033[0m"
cd qemu-7.0.0
echo -e "\033[41;37m 开始编译安装 QEMU \033[0m"
git apply ../lzuoslab-qemu7.0.0.A.patch
git apply ../lzuoslab-qemu7.0.0.B.patch
./configure --target-list=riscv32-softmmu,riscv64-softmmu
make -j$(nproc)
sudo make install
echo -e "\033[41;37m 开始编译安装 GDB \033[0m"
cd ../gdb-12.1/
git apply ../lzuoslab-gdb12.1.patch
./configure --target=riscv64-unknown-elf --enable-tui=yes -with-python=python3
make -j$(nproc)
sudo make install
echo -e "\033[41;37m GDB安装完成，版本信息： \033[0m"
riscv64-unknown-elf-gdb -v
echo -e "\033[41;37m QEMU安装完成，版本信息： \033[0m"
qemu-system-riscv64 --version
cd ../..
