# 使用官方 Python slim 作为基础镜像
FROM python:3.11-slim

WORKDIR /app

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_INDEX_URL=https://mirrors.aliyun.com/pypi/simple \
    PIP_TRUSTED_HOST=mirrors.aliyun.com

# 使用国内镜像源，加速 apt（python:3.11-slim 使用 /etc/apt/sources.list.d/debian.sources）
RUN sed -i 's@http://deb.debian.org@http://mirrors.aliyun.com@g' /etc/apt/sources.list.d/debian.sources || true && \
    sed -i 's@https://deb.debian.org@http://mirrors.aliyun.com@g' /etc/apt/sources.list.d/debian.sources || true && \
    apt-get update && apt-get install -y --no-install-recommends gcc && \
    rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app.py ./app.py
COPY static ./static

EXPOSE 5000

HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
  CMD curl -f http://localhost:5000/api/state || exit 1

CMD ["python", "app.py"]
