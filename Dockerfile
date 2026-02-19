FROM gitlab-vs.informatik.uni-stuttgart.de:5050/emergency_traffic/d6g:latest

COPY . /usr/src/fips

WORKDIR /usr/src/fips

RUN apt-get update && \
    apt-get -y install cmake nlohmann-json3-dev python3-venv && \
    python3 -m venv /venv && \
    pip install -Ur /usr/src/fips/python_requirements.txt

ENV PATH="/venv/bin:$PATH"

ENV CC=/usr/local/bin/gcc
ENV CXX=/usr/local/bin/g++

RUN mkdir /usr/src/fips/release && \
    cd /usr/src/fips/release && \
    cmake -DNDEBUG .. && \
    make -j

CMD ["/bin/bash"]
