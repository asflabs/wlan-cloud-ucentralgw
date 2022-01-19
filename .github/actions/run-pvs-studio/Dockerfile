FROM ucloud-builder AS pvs-studio
WORKDIR /pvs-studio

ADD .github/actions/entrypoint.sh /entrypoint.sh

WORKDIR /pvs_workdir
RUN wget https://cdn.pvs-studio.com/pvs-studio-7.16.55312.179-x86_64.tgz
RUN tar -xf pvs-studio-7.16.55312.179-x86_64
RUN ./pvs-studio-7.16.55312.179-x86_64./install.sh
RUN pvs-studio-analyzer credentials PVS-Studio Free FREE-FREE-FREE-FREE

ENTRYPOINT [ "/entrypoint.sh" ]