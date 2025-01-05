FROM docker.io/gcc:latest

COPY . /usr/src/libtsndgm
WORKDIR /usr/src/libtsndgm

RUN apt-get update && apt-get -y install cmake nlohmann-json3-dev
RUN rm -rf build && mkdir build && cd build && cmake .. && make -j

CMD ["/bin/bash"]
