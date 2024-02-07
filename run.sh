sudo docker run -v .:/build ghcr.io/mattebit/contiki-cooja-compile-compiler:main

xhost +local:
sudo docker run \
      -v /tmp/.X11-unix/:/tmp/.X11-unix/ \
      -v /etc/localtime:/etc/localtime:ro \
      -v "$HOME/.Xauthority:/root/.Xauthority:rw" \
      -v ./cooja_folder:/contiki/tools/cooja/build \
      -e DISPLAY=$DISPLAY \
      -v .:/build \
      ghcr.io/mattebit/contiki-cooja-compile-cooja:main
xhost -local: