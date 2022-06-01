sudo dnf install g++ ncurses-devel python3-devel ninja-build texinfo git
sudo dnf install autoconf automake python3 libmpc-devel mpfr-devel gmp-devel gawk  bison flex texinfo patchutils gcc gcc-c++ zlib-devel expat-devel

cd resource
wget https://download.qemu.org/qemu-7.0.0.tar.xz
wget https://mirror.lzu.edu.cn/gnu/gdb/gdb-12.1.tar.xz

tar xJf qemu-7.0.0.tar.xz
tar xJf gdb-12.1.tar.xz

git clone https://github.com/riscv/riscv-gnu-toolchain
cd riscv-gnu-toolchain
./configure --prefix=/opt/riscv
sudo make -j$(nproc)
echo 'PATH=$PATH:/opt/riscv/bin' >> ~/.bashrc

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
