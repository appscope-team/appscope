FROM alpine:latest

RUN apk add bash binutils curl gcc gdb musl-dev openssl socat

RUN mkdir -p /opt/fwrite
COPY ./musl/fwrite.c /opt/fwrite/fwrite.c
RUN gcc -o /opt/fwrite/fwrite /opt/fwrite/fwrite.c

RUN mkdir -p /opt/fault_test
COPY ./musl/hello_fault.c /opt/fault_test/hello_fault.c
RUN gcc -O0 -g -o /opt/fault_test/fault_test /opt/fault_test/hello_fault.c -lrt

RUN mkdir -p /opt/sig_test
COPY ./musl/sigusrhandler.c /opt/sig_test/sigusrhandler.c
RUN gcc -g -o /opt/sig_test/sigusrhandler /opt/sig_test/sigusrhandler.c

RUN mkdir -p /opt/extract_scope && \
    mkdir -p /opt/patch_libscope

ENV SCOPE_CRIBL_ENABLE=false
ENV SCOPE_LOG_LEVEL=error
ENV SCOPE_METRIC_VERBOSITY=4
ENV SCOPE_EVENT_LOGFILE=true
ENV SCOPE_EVENT_CONSOLE=true
ENV SCOPE_EVENT_METRIC=true
ENV SCOPE_EVENT_HTTP=true
ENV SCOPE_EVENT_NET=true
ENV SCOPE_EVENT_FS=false
ENV SCOPE_LOG_DEST=file:///opt/test-runner/logs/scope.log
ENV SCOPE_EVENT_DEST=file:///opt/test-runner/logs/events.log

ENV PATH="/usr/local/scope:/usr/local/scope/bin:${PATH}"
RUN echo "export PATH=/usr/local/scope:/usr/local/scope/bin:${PATH}" >/etc/profile.d/path.sh

COPY scope-profile.sh /etc/profile.d/scope.sh
COPY gdbinit /root/.gdbinit

RUN  mkdir /usr/local/scope && \
     mkdir /usr/local/scope/bin && \
     mkdir /usr/local/scope/lib && \
     mkdir -p /opt/test-runner/logs/ && \
     ln -s /opt/appscope/bin/linux/$(uname -m)/scope /usr/local/scope/bin/scope && \
     ln -s /opt/appscope/lib/linux/$(uname -m)/libscope.so /usr/local/scope/lib/libscope.so

COPY musl/scope-test /usr/local/scope/scope-test


RUN (cd /usr/local/scope &&  \
     openssl req -nodes -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem \
      -subj "/C=US/ST=MN/L=Excelsior/O=Cribl/OU=Engineering/CN=cribl.io")


COPY docker-entrypoint.sh /
ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["test"]
