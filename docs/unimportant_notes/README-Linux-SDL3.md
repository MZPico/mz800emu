# Linux — SDL3 build from source

## Dependencies

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3-pip \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libwayland-dev wayland-protocols libdrm-dev libgbm-dev \
  libasound2-dev libpulse-dev libaudio-dev \
  libxrender-dev libxfixes-dev libxss-dev libdbus-1-dev \
  libudev-dev libgles2-mesa-dev libegl1-mesa-dev \
  libibus-1.0-dev fcitx-libs-dev libsamplerate0-dev \
  libpipewire-0.3-dev libdecor-0-dev git
```

## SDL3

```sh
cd /usr/local/src
git clone https://github.com/libsdl-org/SDL.git
cd SDL
git checkout main
mkdir build && cd build
cmake .. -G Ninja -DSDL_TEST=ON -DSDL_SHARED=ON -DSDL_STATIC=OFF
ninja
sudo ninja install
sudo ldconfig
pkg-config --modversion sdl3
```

### Alternativní instalační adresář

```sh
cmake .. -G Ninja -DCMAKE_INSTALL_PREFIX=/opt/SDL3
sudo ninja install
```

## SDL3_image

```sh
cd /usr/local/src
git clone https://github.com/libsdl-org/SDL_image.git
cd SDL_image
git checkout main
mkdir build && cd build
cmake .. -DSDL3IMAGE_VENDORED=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc)
sudo make install
pkg-config --modversion sdl3-image
```
