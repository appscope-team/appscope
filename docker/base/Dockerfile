FROM ubuntu:20.04

RUN apt update -y && \
    apt install -y bash-completion ca-certificates && \
    apt upgrade -y libsystemd0 libudev1 && \
    apt clean && \
    rm -rf /var/lib/apt/lists/*

ARG TARGETARCH
COPY bin/linux/${TARGETARCH}/scope       /usr/local/bin/scope
COPY lib/linux/${TARGETARCH}/libscope.so /usr/local/lib/libscope.so

RUN mkdir /usr/local/scope /usr/local/scope/x86_64 \
 && ln -s x86_64 /usr/local/scope/amd64 \
   
COPY bin/linux/x86_64/* /usr/local/scope/x86_64/
COPY conf/scope.yml     /usr/local/scope/x86_64/scope.yml

RUN rm -f /usr/local/scope/*/.gitignore
RUN echo "source /etc/profile.d/bash_completion.sh" >> ~/.bashrc && \
    echo "source <(/usr/local/bin/scope completion bash)" >> ~/.bashrc
