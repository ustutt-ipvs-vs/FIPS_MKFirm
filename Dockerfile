FROM docker.io/gcc:latest

WORKDIR /usr/src/libtsndgm

RUN apt-get update && apt-get -y install cmake nlohmann-json3-dev python3-venv
RUN python3 -m venv /venv

ENV PATH="/venv/bin:$PATH"
COPY ./python_requirements.txt /usr/src/python_requirements.txt
RUN pip install -Ur /usr/src/python_requirements.txt

CMD ["/bin/bash"]
