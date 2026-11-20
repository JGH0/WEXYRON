FROM ubuntu:22.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libglfw3-dev \
    libglm-dev \
    libglew-dev \
    libgl1-mesa-dev \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY CMakeLists.txt ./
COPY server_main.cpp ./
COPY src/ ./src/
COPY include/ ./include/
COPY engine/ ./engine/
COPY third_party/ ./third_party/
COPY glad.c ./

# Copy assets
COPY assets/ ./assets/

# Build the server
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SERVER_ONLY=ON && \
    make -j$(nproc)

# Create logs directory
RUN mkdir -p /app/logs

# Copy server config template
COPY server_config.json ./

# Expose server port
EXPOSE 7777/udp

# Run the server
CMD ["./build/bin/server", "7777"]
