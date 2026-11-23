# Base: any Ubuntu 18.04 image with the AOSP 8.1 host packages, Python 2.7 and OpenJDK 8.
FROM android865_build:18.04
USER root
RUN sed -i 's#http://[a-z.]*ubuntu.com#http://mirrors.tuna.tsinghua.edu.cn#g' /etc/apt/sources.list && apt-get update -q && apt-get install -y -q curl python3.8 && ln -sf /usr/bin/python3.8 /usr/bin/python3 && rm -rf /var/lib/apt/lists/*
RUN for g in /etc/java-8-openjdk/security/java.security; do sed -i 's/^\(jdk.tls.disabledAlgorithms=\)SSLv3, TLSv1, TLSv1.1,/\1SSLv3,/' $g; done
USER u
