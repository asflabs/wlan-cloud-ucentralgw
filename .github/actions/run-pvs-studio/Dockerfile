FROM ucloud-builder AS pvs-studio
WORKDIR /pvs-studio

ADD entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

WORKDIR /pvs_workdir
RUN wget https://cdn.pvs-studio.com/pvs-studio-7.16.55312.179-x86_64.tgz
RUN tar -xf pvs-studio-7.16.55312.179-x86_64.tgz
WORKDIR /pvs_workdir/pvs-studio-7.16.55312.179-x86_64
RUN ./install.sh

ENTRYPOINT [ "/entrypoint.sh" ]