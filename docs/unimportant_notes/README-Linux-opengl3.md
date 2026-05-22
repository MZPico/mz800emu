# Linux — OpenGL 3 dependencies

```sh
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev
```

- `libgl1-mesa-dev` — OpenGL (Mesa)
- `libglu1-mesa-dev` — GLU (may be needed)
- `libx11-dev`, `libxrandr-dev`, `libxi-dev`, `libxinerama-dev`, `libxcursor-dev` — dependencies for SDL, GLFW, etc.

### NVIDIA

```sh
sudo apt install -y nvidia-cuda-toolkit
```

### Window library — GLFW

```sh
sudo apt install -y libglfw3-dev
# link flags: -lglfw -lGL
```

### Window library — SDL3

```sh
sudo apt install -y libsdl3-dev
# link flags: $(pkg-config --cflags --libs sdl3) -lGL
```

### OpenGL ES 3 (GLES)

For GLES instead of OpenGL 3, install `libgles2-mesa-dev` and change `-lGL` to `-lGLESv2`.
