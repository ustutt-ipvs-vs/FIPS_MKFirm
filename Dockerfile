FROM docker.io/gcc:latest

ARG INSTALL_OMNETPP="true"

WORKDIR /usr/src

RUN apt-get update && apt-get -y install cmake nlohmann-json3-dev python3-venv
RUN python3 -m venv /venv

ENV PATH="/venv/bin:$PATH"
COPY ./python_requirements.txt /usr/src/python_requirements.txt
RUN pip install -Ur /usr/src/python_requirements.txt

COPY ./scripts/docker/install_omnetpp.sh /usr/src/install_omnetpp.sh
COPY ./scripts/docker/patch.diff /usr/src/patch.diff
RUN /usr/src/install_omnetpp.sh

WORKDIR /usr/src/fips
ENV CC=/usr/local/bin/gcc
ENV CXX=/usr/local/bin/g++

CMD ["/bin/bash"]
