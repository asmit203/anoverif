FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libmicrohttpd-dev \
    libjsoncpp-dev \
    libssl-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Create app user
RUN useradd -m -s /bin/bash anoverif

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Change ownership
RUN chown -R anoverif:anoverif /app

# Switch to app user
USER anoverif

# Build the application
RUN ./build.sh

# Expose ports
EXPOSE 8080 8443

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Default command
ENTRYPOINT ["./build/anon_server"]
