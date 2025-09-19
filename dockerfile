FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends build-essential curl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN make
EXPOSE 8080
CMD ["./htproxy", "-p", "8080", "-c"]