FROM gitlab-vs.informatik.uni-stuttgart.de:5050/emergency_traffic/d6g:latest

WORKDIR /usr/src

RUN apt-get update && apt-get -y install cmake nlohmann-json3-dev python3-venv
RUN python3 -m venv /venv

ENV PATH="/venv/bin:$PATH"
COPY ./python_requirements.txt /usr/src/python_requirements.txt
RUN pip install -Ur /usr/src/python_requirements.txt

WORKDIR /usr/src/fips
ENV CC=/usr/local/bin/gcc
ENV CXX=/usr/local/bin/g++

CMD ["/bin/bash"]
