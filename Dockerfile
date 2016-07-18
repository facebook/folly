FROM ubuntu:trusty

ADD build_preq.sh /tmp/build_preq.sh
RUN chmod +x /tmp/build_preq.sh
RUN /bin/bash -c /tmp/build_preq.sh

