ARG OL_VERSION=9
FROM oraclelinux:${OL_VERSION} AS build
COPY packaging/oracle-linux/install-deps.sh /tmp/install-deps.sh
RUN bash /tmp/install-deps.sh
WORKDIR /src
COPY . .
RUN OUTPUT_DIR=/out bash packaging/oracle-linux/build.sh

FROM scratch AS artifact
COPY --from=build /out/ /
