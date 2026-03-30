FROM python:3.12-slim

RUN pip install --no-cache-dir conan_server

EXPOSE 9300

VOLUME /root/.conan_server

CMD ["conan_server"]
