FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get -y update && apt-get -y install clang-format
COPY run-format.sh /

CMD ["/run-format.sh"]
