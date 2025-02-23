# Arch Linux
It does not get much simpler than the following, in case you still needed a reason to switch distros :-)
1. Install dependencies.
```bash
$ pacman -Syu git make cmake python gcc
```
2. Build the project. Enter the project directory
```bash
$ mkdir release
$ cd release
$ cmake ..
$ make -j
```

# Ubuntu (Noble Nomat 24.04)
1. Install dependencies.
```bash
$ apt-get update
$ apt-get install git cmake aptitude python3-venv gcc-14 g++-14
```
In case gcc-14/g++-14 are not found, try to add the `universe` repository to the apt sources.
2. Build the project. Enter the project directory
```bash
$ export CC=/usr/bin/gcc-14
$ export CXX=/usr/bin/g++-14
$ mkdir release
$ cd release
$ cmake ..
$ make -j
```


# Debian (Bookworm)
Debian's *stable* distribution is still stuck at gcc-12, which does not support C++23. 
We experienced similar problems with clang-19, where some std libraries were not fully supported yet. 
In the following, we show how to install gcc-14 that is currently still part of Debian's *testing* distribution.

1. Install dependencies. 
```bash
$ apt-get update
$ apt-get install git cmake aptitude python3-venv
```
2. Setup apt preference to install individual *testing* packages without moving the entire system from *stable*. 
Create the following files in `/etc/apt/preferences.d` (taken from [here](https://serverfault.com/questions/22414/how-can-i-run-debian-stable-but-install-some-packages-from-testing))
 - `stable.pref`: 
```
Package: *
Pin: release a=stable
Pin-Priority: 900
```
 - `testing.pref`: 
```
Package: *
Pin: release a=testing
Pin-Priority: 400
```
3. Add apt sources for *stable* and *testing*.
Create the following files in `/etc/apt/sources.list.d`
 - `stable.list` 
```
deb     http://ftp.de.debian.org/debian/    stable main contrib non-free
deb-src http://ftp.de.debian.org/debian/    stable main contrib non-free

deb     http://security.debian.org/         stable/updates  main contrib non-free
```
 - `testing.list` 
```
deb     http://ftp.de.debian.org/debian/    testing main contrib non-free
deb-src http://ftp.de.debian.org/debian/    testing main contrib non-free

deb     http://security.debian.org/         testing/updates  main contrib non-free
```
4. Install gcc-14 with aptitude. Compared to apt, aptitude appears to manage dependencies better.
```bash
$ aptitude update
$ aptitude install gcc-14 g++-14
The following NEW packages will be installed:
  g++-14{b} gcc-14{b} gcc-14-base{a} 
0 packages upgraded, 3 newly installed, 0 to remove and 0 not upgraded.
Need to get 608 kB of archives. After unpacking 783 kB will be used.
The following packages have unmet dependencies:
 g++-14 : Depends: g++-14-x86-64-linux-gnu (= 14.2.0-16) but it is not installable
 gcc-14 : Depends: gcc-14-x86-64-linux-gnu (= 14.2.0-16) but it is not installable
          Depends: cpp-14 (= 14.2.0-16) but it is not installable
The following actions will resolve these dependencies:

     Keep the following packages at their current version:
1)     g++-14 [Not Installed]                             
2)     gcc-14 [Not Installed]                             



Accept this solution? [Y/n/q/?] n
The following actions will resolve these dependencies:

      Install the following packages:                                         
1)      cpp-14 [14.2.0-16 (testing)]                                          
2)      cpp-14-x86-64-linux-gnu [14.2.0-16 (testing)]                         
3)      g++-14-x86-64-linux-gnu [14.2.0-16 (testing)]                         
4)      gcc-14-x86-64-linux-gnu [14.2.0-16 (testing)]                         
5)      libgcc-14-dev [14.2.0-16 (testing)]                                   
6)      libhwasan0 [14.2.0-16 (testing)]                                      
7)      libstdc++-14-dev [14.2.0-16 (testing)]                                

      Upgrade the following packages:                                         
8)      base-files [12.4+deb12u9 (now, stable) -> 13.6 (testing)]             
9)      libasan8 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]             
10)     libatomic1 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]           
11)     libc-bin [2.36-9+deb12u9 (now, stable) -> 2.40-7 (testing)]           
12)     libc-dev-bin [2.36-9+deb12u9 (now, stable) -> 2.40-7 (testing)]       
13)     libc6 [2.36-9+deb12u9 (now, stable) -> 2.40-7 (testing)]              
14)     libc6-dev [2.36-9+deb12u9 (now, stable) -> 2.40-7 (testing)]          
15)     libcc1-0 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]             
16)     libgcc-s1 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]            
17)     libgmp10 [2:6.2.1+dfsg1-1.1 (now, stable) -> 2:6.3.0+dfsg-3 (testing)]
18)     libgomp1 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]             
19)     libitm1 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]              
20)     liblsan0 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]             
21)     libquadmath0 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]         
22)     libstdc++6 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]           
23)     libtsan2 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]             
24)     libubsan1 [12.2.0-14 (now, stable) -> 14.2.0-16 (testing)]            
25)     libzstd1 [1.5.4+dfsg2-5 (now, stable) -> 1.5.6+dfsg-2 (testing)]      

Accept this solution? [Y/n/q/?] Y
```
5. Build the project. Enter the project directory
```bash
$ export CC=/usr/bin/gcc-14
$ export CXX=/usr/bin/g++-14
$ mkdir release
$ cd release
$ cmake ..
$ make -j
```
